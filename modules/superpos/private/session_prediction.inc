// Private native prediction implementation; never a generated SDK header.
class SuperposSimulationScope {
    SuperposSession *session;
    Ref<SuperposSession> retained;
public:
    explicit SuperposSimulationScope(SuperposSession *owner) : session(owner) {
        session->simulation_in_flight = true;
        ++session->callbacks_in_flight;
        retained = Ref<SuperposSession>(owner);
    }
    ~SuperposSimulationScope() {
        session->simulation_in_flight = false;
        session->_end_callback();
    }
};

Error SuperposSession::_simulation_preflight(uint64_t binding, uint64_t epoch, bool registered) const {
    if (Thread::get_caller_id() != owner_thread) { return ERR_BUSY; }
    if (owner_retired) { return ERR_UNCONFIGURED; }
    if (managed_reload_paused || closing || simulation_in_flight) { return ERR_BUSY; }
    if (callbacks_in_flight && (!registered || !simulation_tick_phase || callbacks_in_flight != 1)) { return ERR_BUSY; }
    if (!Thread::is_main_thread()) { return ERR_UNAVAILABLE; }
    if (!impl || !impl->world) { return ERR_UNCONFIGURED; }
    if (impl->binding_generation != binding) { return ERR_INVALID_PARAMETER; }
    auto current = impl->world->world().authority_epoch();
    if (!current) { return translate(current.error()); }
    if (*current != epoch) { return ERR_INVALID_PARAMETER; }
    if (registered && !impl->prediction) { return ERR_UNCONFIGURED; }
    return OK;
}
bool SuperposSession::_simulation_gate_ready(uint64_t binding, uint64_t epoch) const {
    if (Thread::get_caller_id() != owner_thread || owner_retired || managed_reload_paused || closing ||
            !simulation_in_flight || !callbacks_in_flight || !impl || !impl->world || impl->binding_generation != binding) { return false; }
    auto current = impl->world->world().authority_epoch();
    return current && *current == epoch;
}

Error SuperposSession::register_native_simulation_checked(const Ref<SuperposSimulationProvider> &provider,
        uint64_t binding, uint64_t epoch, uint32_t history_ticks, uint32_t required_history_ticks,
        uint64_t initial_tick, uint64_t revision, const PackedByteArray &initial, uint64_t history_budget) {
    const Error admitted = _simulation_preflight(binding, epoch, false);
    if (admitted != OK) { return admitted; }
    if (impl->prediction) { return ERR_ALREADY_IN_USE; }
#ifdef SUPERPOS_HAS_DTLS
    if (impl->network) { return ERR_ALREADY_IN_USE; }
#endif
    if (!history_ticks || history_ticks > 4096 || !required_history_ticks || required_history_ticks > history_ticks ||
            !history_budget || history_budget > (512ULL << 20) || !revision || provider.is_null()) { return ERR_INVALID_PARAMETER; }
    auto next_binding = superpos::increment(binding);
    if (!next_binding) { return translate(next_binding.error()); }
    SuperposSimulationScope scope(this);
    if (!_simulation_gate_ready(binding, epoch)) { return ERR_UNCONFIGURED; }
    // Freeze once. Holding the incoming provider is part of the same guarded
    // candidate transaction, including reference callbacks and destruction.
    Ref<SuperposSimulationProvider> pinned_provider(provider);
    if (!_simulation_gate_ready(binding, epoch)) { return ERR_UNCONFIGURED; }
    const auto descriptor = pinned_provider->descriptor();
    if (!_simulation_gate_ready(binding, epoch)) { return ERR_UNCONFIGURED; }
    if (auto valid = superpos_egp::validate_simulation_descriptor(descriptor); !valid) { return translate(valid.error()); }
    auto fingerprint = superpos_egp::simulation_fingerprint(impl->digest, impl->world->fingerprint(), descriptor);
    if (!fingerprint) { return translate(fingerprint.error()); }
    if (!_simulation_gate_ready(binding, epoch)) { return ERR_UNCONFIGURED; }
    // The stable Impl record outlives every bridge/history call. Failure clears
    // only the unpublished candidate and does not consume a binding generation.
    impl->prediction_context = {this, binding, epoch};
    const superpos_egp::SimulationGate gate{&impl->prediction_context, [](void *value) noexcept {
        auto *context = static_cast<Impl::PredictionContext *>(value);
        return context->owner->_simulation_gate_ready(context->binding, context->epoch);
    }};
    auto candidate = superpos_egp::NativePredictionOwner::create(impl->allocator, superpos_egp::module_backing(), size_t(history_budget), pinned_provider, descriptor, gate);
    if (!candidate) { return translate(candidate.error()); }
    if (!_simulation_gate_ready(binding, epoch)) { return ERR_UNCONFIGURED; }
    auto initialized = (*candidate)->initialize(epoch, history_ticks, initial_tick, revision, byte_view(initial));
    if (!initialized) { return translate(initialized.error()); }
    // Drop the temporary provider pin while the unpublished candidate still
    // owns it. Its ordinary reference-decrement callbacks remain inside this
    // barrier and can revoke the World before the final publication gate.
    pinned_provider.unref();
    if (!_simulation_gate_ready(binding, epoch)) { return ERR_UNCONFIGURED; }
    impl->prediction = std::move(*candidate);
    impl->prediction_fingerprint = *fingerprint;
    impl->prediction_history_ticks = history_ticks;
    impl->prediction_required_history_ticks = required_history_ticks;
    impl->binding_generation = *next_binding;
    impl->prediction_context.binding = *next_binding;
    return OK;
}

Error SuperposSession::predict_checked(uint64_t binding, uint64_t epoch, uint64_t tick, const PackedByteArray &input) {
    const Error admitted = _simulation_preflight(binding, epoch, true);
    if (admitted != OK) { return admitted; }
    SuperposSimulationScope scope(this);
    if (!_simulation_gate_ready(binding, epoch)) { return ERR_UNCONFIGURED; }
    auto predicted = impl->prediction->predict(epoch, tick, byte_view(input));
    return predicted ? OK : translate(predicted.error());
}

Dictionary SuperposSession::reconcile_checked(uint64_t binding, uint64_t epoch, uint64_t tick,
        uint64_t revision, const PackedByteArray &canonical) {
    Dictionary result;
    const Error admitted = _simulation_preflight(binding, epoch, true);
    result["error"] = admitted;
    if (admitted != OK) { return result; }
    SuperposSimulationScope scope(this);
    if (!_simulation_gate_ready(binding, epoch)) { result["error"] = ERR_UNCONFIGURED; return result; }
    auto repaired = impl->prediction->reconcile(epoch, tick, revision, byte_view(canonical));
    result["error"] = repaired ? OK : translate(repaired.error());
    if (repaired) {
        result["changed"] = repaired->changed;
        result["replayed_ticks"] = int64_t(repaired->replayed_ticks);
    }
    return result;
}

Error SuperposSession::copy_prediction_checked(uint64_t binding, uint64_t epoch,
        PackedByteArray &output, uint64_t &tick) {
    const Error admitted = _simulation_preflight(binding, epoch, true);
    if (admitted != OK) { return admitted; }
    SuperposSimulationScope scope(this);
    if (!_simulation_gate_ready(binding, epoch)) { return ERR_UNCONFIGURED; }
    auto current = impl->prediction->current(epoch);
    if (!current) { return translate(current.error()); }
    PackedByteArray staged;
    const Error resized = staged.resize(int(current->canonical.size()));
    if (resized != OK) { return resized; }
    if (!_simulation_gate_ready(binding, epoch)) { return ERR_UNCONFIGURED; }
    memcpy(staged.ptrw(), current->canonical.data(), current->canonical.size());
    output = std::move(staged);
    tick = current->tick;
    return OK;
}

Dictionary SuperposSession::read_prediction(uint64_t binding, uint64_t epoch) {
    Dictionary result;
    PackedByteArray canonical;
    uint64_t tick = 0;
    const Error error = copy_prediction_checked(binding, epoch, canonical, tick);
    result["error"] = error;
    if (error == OK) { result["canonical"] = canonical; result["tick"] = superpos_egp::signed_bits(tick); }
    return result;
}

Dictionary SuperposSession::read_prediction_info(uint64_t binding, uint64_t epoch) {
    Dictionary result;
    const Error admitted = _simulation_preflight(binding, epoch, true);
    result["error"] = admitted;
    if (admitted != OK) { return result; }
    SuperposSimulationScope scope(this);
    if (!_simulation_gate_ready(binding, epoch)) { result["error"] = ERR_UNCONFIGURED; return result; }
    auto pending = impl->prediction->pending();
    auto charged = impl->prediction->charged_bytes();
    auto capacity = impl->prediction->capacity();
    if (!pending || !charged || !capacity) {
        result["error"] = translate(!pending ? pending.error() : !charged ? charged.error() : capacity.error());
        return result;
    }
    result["pending_ticks"] = int64_t(*pending);
    result["charged_history_bytes"] = int64_t(*charged);
    result["history_ticks"] = *capacity;
    return result;
}

Dictionary SuperposSession::read_simulation_profile() const {
    Dictionary result;
    result["error"] = ERR_UNCONFIGURED;
    if (Thread::get_caller_id() != owner_thread) { result["error"] = ERR_BUSY; return result; }
    if (owner_retired) { return result; }
    if (simulation_in_flight || managed_reload_paused || closing || callbacks_in_flight) { result["error"] = ERR_BUSY; return result; }
    if (!impl || !impl->world) { return result; }
    result["error"] = OK;
    result["registered"] = bool(impl->prediction);
    result["recovery"] = "None";
    result["capabilities"] = impl->prediction ? int64_t(3) : int64_t(0);
    result["history_ticks"] = impl->prediction_history_ticks;
    result["required_history_ticks"] = impl->prediction_required_history_ticks;
    if (impl->prediction) { result["simulation_sha256"] = String::hex_encode_buffer(
        reinterpret_cast<const uint8_t *>(impl->prediction_fingerprint.data()), impl->prediction_fingerprint.size()); }
    return result;
}
