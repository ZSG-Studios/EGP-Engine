#include "superpos/dtls.hpp"
#include "superpos/codec.hpp"
#include <mbedtls/ssl.h>
#include <mbedtls/ssl_cookie.h>
#include <mbedtls/ssl_ciphersuites.h>
#include <psa/crypto.h>
#include <mbedtls/platform_util.h>
#include <new>
#include <cstring>
#include <utility>
namespace superpos {
CryptoRuntime::CryptoRuntime(CryptoOwnership o) noexcept:owns_(o==CryptoOwnership::Standalone){}
CryptoRuntime::~CryptoRuntime(){/* PSA globals remain process-owned until exit. Borrowed mode never frees them. */}
Status CryptoRuntime::initialize() noexcept {
 if(owns_&&psa_crypto_init()!=PSA_SUCCESS)return fail(Error::AuthenticationFailed);
 // Borrowed mode verifies existing host initialization without acquiring it.
 unsigned char probe{};auto status=psa_generate_random(&probe,1);mbedtls_platform_zeroize(&probe,1);
 return status==PSA_SUCCESS?Status{}:Status(fail(Error::NotReady));
}
struct DtlsAssociation::Impl {
 mbedtls_ssl_context ssl;mbedtls_ssl_config config;mbedtls_ssl_cookie_ctx cookie;
 Clock* clock;DatagramIO* io;DtlsConfig settings;
 std::array<std::byte,128> address{};std::size_t address_size{};
 std::array<std::byte,32> key{};std::array<std::byte,8> identity{};
 std::array<std::byte,DtlsAssociation::maximum_frame_bytes> pending{};std::size_t pending_size{};
 std::uint64_t started{},timer_origin{};std::uint32_t intermediate{},final{};
 bool connected{},failed{};
 DtlsStatistics stats{};
 // Path validation records (path-aware IO): one 17-byte control record queued
 // or in flight at a time, written to the current path or one candidate.
 static constexpr std::byte challenge_tag{0x16},response_tag{0x17};
 static constexpr std::size_t control_bytes=17;
 struct Control { std::array<std::byte,control_bytes> bytes{}; bool queued{},inflight{},candidate{}; std::uint64_t generation{}; } control{};
 bool route_candidate{};std::uint64_t route_generation{};
 struct Challenge { bool active{}; std::uint64_t generation{},sent_at{},retry_after{}; unsigned attempts{}; std::array<std::byte,16> nonce{}; } challenge{};
 Impl(Clock& c,DatagramIO& d,DtlsConfig s) noexcept:clock(&c),io(&d),settings(s){mbedtls_ssl_init(&ssl);mbedtls_ssl_config_init(&config);mbedtls_ssl_cookie_init(&cookie);started=c.now_ms();}
 ~Impl(){mbedtls_ssl_free(&ssl);mbedtls_ssl_config_free(&config);mbedtls_ssl_cookie_free(&cookie);mbedtls_platform_zeroize(key.data(),key.size());mbedtls_platform_zeroize(pending.data(),pending.size());}
 static int write(void* p,const unsigned char* b,std::size_t n) noexcept {
  auto& self=*static_cast<Impl*>(p);if(!n||n>self.settings.udp_payload_ceiling)return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
  const std::span<const std::byte> bytes{reinterpret_cast<const std::byte*>(b),n};
  if(self.route_candidate){
   // A challenge or response for a candidate path. A refused send (address
   // amplification limit) or a superseded candidate is that record's loss.
   auto r=self.io->send_candidate(self.route_generation,bytes);
   if(r||r.error()==Error::CapacityExceeded||r.error()==Error::StaleGeneration)return static_cast<int>(n);
   return r.error()==Error::Busy?MBEDTLS_ERR_SSL_WANT_WRITE:MBEDTLS_ERR_SSL_INTERNAL_ERROR;
  }
  auto r=self.io->send(bytes);
  // A datagram the local path refuses as too large (EMSGSIZE with DF set) is
  // the same evidence as an in-network drop: the record is consumed, and path
  // MTU probing above this layer observes the missing acknowledgement.
  if(!r&&r.error()==Error::CapacityExceeded){++self.stats.path_oversize_drops;return static_cast<int>(n);}
  if(!r)return r.error()==Error::Busy?MBEDTLS_ERR_SSL_WANT_WRITE:MBEDTLS_ERR_SSL_INTERNAL_ERROR;
  // Datagram callbacks must never turn one record into multiple partial sends.
  if(*r!=n)return MBEDTLS_ERR_SSL_INTERNAL_ERROR;return static_cast<int>(*r);
 }
 static int read(void* p,unsigned char* b,std::size_t n) noexcept {
  auto& self=*static_cast<Impl*>(p);auto r=self.io->receive({reinterpret_cast<std::byte*>(b),n});
  if(!r)return r.error()==Error::Busy||r.error()==Error::CapacityExceeded?MBEDTLS_ERR_SSL_WANT_READ:MBEDTLS_ERR_SSL_INTERNAL_ERROR;
  if(!*r)return MBEDTLS_ERR_SSL_WANT_READ;
  if(*r>n||*r>self.settings.udp_payload_ceiling)return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
  return static_cast<int>(*r);
 }
 static void set_timer(void*p,std::uint32_t i,std::uint32_t f) noexcept {auto& s=*static_cast<Impl*>(p);s.timer_origin=s.clock->now_ms();s.intermediate=i;s.final=f;}
 static int get_timer(void*p) noexcept {auto&s=*static_cast<Impl*>(p);if(s.final==0)return -1;auto now=s.clock->now_ms();if(now<s.timer_origin){s.failed=true;return 2;}auto d=now-s.timer_origin;return d>=s.final?2:d>=s.intermediate?1:0;}
 static int psk(void*p,mbedtls_ssl_context* ssl,const unsigned char* id,std::size_t n) noexcept {auto&s=*static_cast<Impl*>(p);if(n!=8||std::memcmp(id,s.identity.data(),8)!=0)return MBEDTLS_ERR_SSL_UNKNOWN_IDENTITY;return mbedtls_ssl_set_hs_psk(ssl,reinterpret_cast<const unsigned char*>(s.key.data()),s.key.size());}
 Status reset_cookie() noexcept {if(mbedtls_ssl_session_reset(&ssl)!=0)return fail(Error::AuthenticationFailed);if(mbedtls_ssl_set_client_transport_id(&ssl,reinterpret_cast<const unsigned char*>(address.data()),address_size)!=0)return fail(Error::AuthenticationFailed);return {};}
 Status flush_application() noexcept {if(!pending_size)return {};int n=mbedtls_ssl_write(&ssl,reinterpret_cast<const unsigned char*>(pending.data()),pending_size);if(n==MBEDTLS_ERR_SSL_WANT_READ||n==MBEDTLS_ERR_SSL_WANT_WRITE)return fail(Error::Busy);if(n<0||static_cast<std::size_t>(n)!=pending_size){failed=true;return fail(Error::AuthenticationFailed);}mbedtls_platform_zeroize(pending.data(),pending_size);pending_size=0;return {};}
 Status write_control() noexcept {
  route_candidate=control.candidate;route_generation=control.generation;
  int n=mbedtls_ssl_write(&ssl,reinterpret_cast<const unsigned char*>(control.bytes.data()),control.bytes.size());
  route_candidate=false;
  if(n==MBEDTLS_ERR_SSL_WANT_READ||n==MBEDTLS_ERR_SSL_WANT_WRITE){control.inflight=true;return fail(Error::Busy);}
  if(n<0||static_cast<std::size_t>(n)!=control.bytes.size()){failed=true;return fail(Error::AuthenticationFailed);}
  control.queued=control.inflight=false;return {};
 }
 // A blocked ssl_write owns its exact arguments until it completes: an
 // in-flight control record finishes first, then application data, then a
 // newly queued control record.
 Status flush() noexcept {
  if(control.inflight)if(auto r=write_control();!r)return r;
  if(auto r=flush_application();!r)return r;
  if(control.queued)if(auto r=write_control();!r)return r;
  return {};
 }
 bool control_free() const noexcept { return !control.queued&&!control.inflight; }
 void send_challenge(std::uint64_t now) noexcept {
  if(!control_free())return; // Retried from advance once the control slot frees.
  control.bytes[0]=challenge_tag;std::memcpy(control.bytes.data()+1,challenge.nonce.data(),challenge.nonce.size());
  control.queued=true;control.candidate=true;control.generation=challenge.generation;
  challenge.sent_at=now;++challenge.attempts;++stats.path_challenges_sent;
 }
 // An authenticated record arrived from a candidate address: challenge it once
 // (three attempts, 500 ms doubling) before any traffic moves there. A failed
 // validation holds off new challenges for five seconds.
 void consider(DatagramPath path,std::uint64_t now) noexcept {
  if(!path.candidate)return;
  if(challenge.active&&challenge.generation==path.generation)return;
  if(now<challenge.retry_after)return;
  if(psa_generate_random(reinterpret_cast<unsigned char*>(challenge.nonce.data()),challenge.nonce.size())!=PSA_SUCCESS)return;
  challenge.active=true;challenge.generation=path.generation;challenge.attempts=0;send_challenge(now);
 }
 void challenge_timers(std::uint64_t now) noexcept {
  if(!challenge.active||!challenge.attempts)return;
  const std::uint64_t timeout=500ULL<<(challenge.attempts-1);
  if(now<challenge.sent_at||now-challenge.sent_at<timeout)return;
  if(challenge.attempts>=3){challenge.active=false;challenge.retry_after=now+5000;++stats.path_challenge_failures;return;}
  send_challenge(now);
 }
 // Consumes path validation records; returns false for application data.
 bool control_record(std::span<const std::byte> record,DatagramPath path,std::uint64_t now) noexcept {
  if(!settings.path_validation||record.size()!=control_bytes)return false;
  if(record[0]==challenge_tag){
   // Answer on the path the challenge used. One control record at a time; a
   // dropped answer is recovered by the challenger's retry.
   if(control_free()){
    control.bytes[0]=response_tag;std::memcpy(control.bytes.data()+1,record.data()+1,control_bytes-1);
    control.queued=true;control.candidate=path.candidate;control.generation=path.generation;++stats.path_responses_sent;
   }
   return true;
  }
  if(record[0]==response_tag){
   if(challenge.active&&path.candidate&&path.generation==challenge.generation&&
      std::memcmp(record.data()+1,challenge.nonce.data(),challenge.nonce.size())==0&&io->promote_candidate(path.generation)){
    challenge.active=false;++stats.path_promotions;
   } else ++stats.stale_path_responses;
   return true;
  }
  (void)now;return false;
 }
};
DtlsAssociation::~DtlsAssociation(){if(impl_){impl_->~Impl();allocator_->deallocate(impl_);}}
DtlsAssociation::DtlsAssociation(DtlsAssociation&&s) noexcept:impl_(std::exchange(s.impl_,nullptr)),allocator_(s.allocator_){}
DtlsAssociation& DtlsAssociation::operator=(DtlsAssociation&&s) noexcept {if(this!=&s){if(impl_){impl_->~Impl();allocator_->deallocate(impl_);}impl_=std::exchange(s.impl_,nullptr);allocator_=s.allocator_;}return *this;}
Result<DtlsAssociation> DtlsAssociation::create(Allocator&a,Clock&c,DatagramIO&io,AuthProvider&auth,DtlsConfig cfg,std::span<const std::byte>address) noexcept {
 // A path-aware server learns its peer address from the first handshake datagram.
 const bool learned=cfg.server&&io.path_aware()&&address.empty();
 if((address.empty()&&!learned)||address.size()>128||(cfg.udp_payload_ceiling<maximum_frame_bytes+37||cfg.udp_payload_ceiling>1200)||cfg.handshake_timeout_ms==0)return fail(Error::InvalidArgument);
 auto* p=a.allocate(sizeof(Impl),alignof(Impl),MemoryDomain::Backend);if(!p)return fail(Error::OutOfMemory);
 DtlsAssociation result;result.allocator_=&a;result.impl_=new(p)Impl(c,io,cfg);auto&s=*result.impl_;
 auto key=auth.admission_key(cfg.identity,s.key);if(!key)return fail(key.error());s.identity=sortable_u64(cfg.identity);
 bool nonzero=false;for(auto b:s.key)nonzero|=b!=std::byte{};if(!nonzero)return fail(Error::AuthenticationFailed);
 std::memcpy(s.address.data(),address.data(),address.size());s.address_size=address.size();
 if(mbedtls_ssl_config_defaults(&s.config,cfg.server?MBEDTLS_SSL_IS_SERVER:MBEDTLS_SSL_IS_CLIENT,MBEDTLS_SSL_TRANSPORT_DATAGRAM,MBEDTLS_SSL_PRESET_DEFAULT)!=0)return fail(Error::AuthenticationFailed);
 static const int suites[]={MBEDTLS_TLS_PSK_WITH_AES_128_GCM_SHA256,0};mbedtls_ssl_conf_ciphersuites(&s.config,suites);
 mbedtls_ssl_conf_min_tls_version(&s.config,MBEDTLS_SSL_VERSION_TLS1_2);mbedtls_ssl_conf_max_tls_version(&s.config,MBEDTLS_SSL_VERSION_TLS1_2);
 mbedtls_ssl_conf_dtls_anti_replay(&s.config,MBEDTLS_SSL_ANTI_REPLAY_ENABLED);
#if defined(MBEDTLS_SSL_RENEGOTIATION)
 mbedtls_ssl_conf_renegotiation(&s.config,MBEDTLS_SSL_RENEGOTIATION_DISABLED);
#endif
 mbedtls_ssl_conf_handshake_timeout(&s.config,100,2000);
 if(cfg.server){if(mbedtls_ssl_cookie_setup(&s.cookie)!=0)return fail(Error::AuthenticationFailed);mbedtls_ssl_conf_dtls_cookies(&s.config,mbedtls_ssl_cookie_write,mbedtls_ssl_cookie_check,&s.cookie);mbedtls_ssl_conf_psk_cb(&s.config,Impl::psk,&s);}
 else if(mbedtls_ssl_conf_psk(&s.config,reinterpret_cast<const unsigned char*>(s.key.data()),s.key.size(),reinterpret_cast<const unsigned char*>(s.identity.data()),8)!=0)return fail(Error::AuthenticationFailed);
 if(mbedtls_ssl_setup(&s.ssl,&s.config)!=0)return fail(Error::OutOfMemory);
 mbedtls_ssl_set_mtu(&s.ssl,static_cast<std::uint16_t>(cfg.udp_payload_ceiling));mbedtls_ssl_set_bio(&s.ssl,&s,Impl::write,Impl::read,nullptr);mbedtls_ssl_set_timer_cb(&s.ssl,&s,Impl::set_timer,Impl::get_timer);
 if(cfg.server&&!learned&&mbedtls_ssl_set_client_transport_id(&s.ssl,reinterpret_cast<const unsigned char*>(address.data()),address.size())!=0)return fail(Error::AuthenticationFailed);
 return result;
}
Result<DtlsStatistics> DtlsAssociation::statistics() const noexcept {if(!impl_)return fail(Error::NotReady);return impl_->stats;}
TransportCapabilities DtlsAssociation::capabilities() const noexcept {return {true,true,true,false,maximum_frame_bytes,37};}
bool DtlsAssociation::ready() const noexcept {return impl_&&impl_->connected&&!impl_->failed;}
Status DtlsAssociation::advance() noexcept {
 if(!impl_||impl_->failed)return fail(Error::NotReady);auto&s=*impl_;
 if(s.connected){s.challenge_timers(s.clock->now_ms());return s.flush();}
 auto now=s.clock->now_ms();if(now<s.started||now-s.started>=s.settings.handshake_timeout_ms){s.failed=true;return fail(Error::Timeout);}
 if(s.settings.server&&s.io->path_aware()){
  // Bind cookies to the address the shared socket observed for this
  // association before the backend reads its datagram.
  if(auto polled=s.io->poll();!polled){s.failed=true;return fail(polled.error());}
  auto identity=s.io->path_identity();
  if(identity.empty())return fail(Error::Busy);
  if(identity.size()>s.address.size()){s.failed=true;return fail(Error::ProtocolViolation);}
  if(identity.size()!=s.address_size||std::memcmp(identity.data(),s.address.data(),identity.size())!=0){
   std::memcpy(s.address.data(),identity.data(),identity.size());s.address_size=identity.size();
   if(auto reset=s.reset_cookie();!reset){s.failed=true;return reset;}
  }
 }
 int r=mbedtls_ssl_handshake(&s.ssl);if(s.failed)return fail(Error::Timeout);if(r==0){s.connected=true;s.io->path_authenticated();return {};}
 if(r==MBEDTLS_ERR_SSL_WANT_READ||r==MBEDTLS_ERR_SSL_WANT_WRITE)return fail(Error::Busy);
 if(r==MBEDTLS_ERR_SSL_HELLO_VERIFY_REQUIRED&&s.settings.server)return s.reset_cookie();
 s.failed=true;return fail(r==MBEDTLS_ERR_SSL_TIMEOUT?Error::Timeout:Error::AuthenticationFailed);
}
Status DtlsAssociation::send(std::span<const std::byte>b) noexcept {if(!ready())return fail(Error::NotReady);if(b.empty()||b.size()>capabilities().maximum_frame)return fail(Error::CapacityExceeded);auto&s=*impl_;if(s.pending_size)return fail(Error::Busy);std::memcpy(s.pending.data(),b.data(),b.size());s.pending_size=b.size();return s.flush();}
Result<std::size_t> DtlsAssociation::receive(std::span<std::byte>b) noexcept {
 if(!ready())return fail(Error::NotReady);auto&s=*impl_;
 // A blocked ssl_write owns its exact arguments until completion; do not enter
 // another SSL operation while its record is still buffered.
 if(auto pending=s.flush();!pending)return fail(pending.error());
 // The BIO admits at most1200 ciphertext bytes. This buffer consumes the entire
 // possible plaintext record, including oversized authenticated records. A
 // rejected record must never leave a suffix exposed as a later game frame.
 std::array<std::byte,1200> packet{};
 // Path validation records are consumed here and never reach the caller.
 for(unsigned record=0;record<8;++record){
  int n=mbedtls_ssl_read(&s.ssl,reinterpret_cast<unsigned char*>(packet.data()),packet.size());
  if(n==MBEDTLS_ERR_SSL_WANT_READ||n==MBEDTLS_ERR_SSL_WANT_WRITE)return fail(Error::Busy);
  if(n<=0){s.failed=true;return fail(Error::ChannelFailed);}
  // The record just authenticated came from the datagram read last.
  const auto path=s.io->received_path();const auto now=s.clock->now_ms();
  const std::span<const std::byte> plain(packet.data(),static_cast<std::size_t>(n));
  if(s.control_record(plain,path,now)){if(auto flushed=s.flush();!flushed&&flushed.error()!=Error::Busy)return fail(flushed.error());continue;}
  if(s.settings.path_validation&&s.io->path_aware())s.consider(path,now);
  if(static_cast<std::size_t>(n)>capabilities().maximum_frame||static_cast<std::size_t>(n)>b.size())return fail(Error::CapacityExceeded);
  std::memcpy(b.data(),packet.data(),static_cast<std::size_t>(n));return static_cast<std::size_t>(n);
 }
 return fail(Error::Busy);
}
}
