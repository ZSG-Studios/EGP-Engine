// SPDX-License-Identifier: MIT
namespace {
struct ServiceTable {
    const char* name;const char* columns;const char* keys;
    std::uint8_t count,key_count;std::array<int,9> bounds;
};
// Positive bounds are exact BLOB widths, negative bounds allow 0..-bound;
// 0 is the nonnegative ordinal INTEGER; nullable retirement columns are
// checked explicitly below. No SQL text or table/column name comes from input.
constexpr std::array<ServiceTable,9> service_tables{{
    {"operation_actors","kind,actor,epoch,peer,first_seq,through,issued,bits","kind,actor",8,2,{8,8,8,8,8,8,8,8}},
    {"pending_operations","kind,actor,actor_epoch,op_seq,peer,request","kind,actor,actor_epoch,op_seq",6,4,{8,8,8,8,8,-4096}},
    {"operation_results","kind,actor,actor_epoch,op_seq,request,result,order_token","kind,actor,actor_epoch,op_seq",7,4,{8,8,8,8,-4096,-4096,8}},
    {"effect_outbox","append_id,ordinal,seq,recipient,data","append_id,ordinal",5,2,{8,0,8,8,-4096}},
    {"control_commits","id,data,confirmation","id",3,1,{8,-4096,8}},
    {"control_policies","session,principal,data","session,principal",3,2,{8,8,-4096}},
    {"control_transitions","id,data","id",2,1,{8,-4096}},
    {"control_leases","data,confirmation","",2,0,{-4096,8}},
    {"authority_grants","seq,nonce,data,confirmation","",4,0,{8,32,147,8}}
}};
constexpr std::size_t service_cursor_bytes=128;
struct ServiceCapture {
    CheckpointServiceProgress progress;
    CoordinatorBootIdentity boot;
    std::array<std::byte,service_cursor_bytes> cursor{};std::size_t cursor_size{};
};
template<class Store> Status service_capture_tables(Store& s) noexcept {
    return execute(s.database,
        "CREATE TABLE IF NOT EXISTS checkpoint_service(match BLOB NOT NULL,id BLOB NOT NULL,generation BLOB NOT NULL,revision BLOB NOT NULL,boot_term BLOB NOT NULL,boot_nonce BLOB NOT NULL,table_index INTEGER NOT NULL,cursor BLOB NOT NULL,bytes BLOB NOT NULL,pages INTEGER NOT NULL,complete INTEGER NOT NULL,root BLOB NOT NULL,cache_order BLOB NOT NULL,PRIMARY KEY(match,id),FOREIGN KEY(match,id) REFERENCES checkpoint_images(match,id)) WITHOUT ROWID;"
        "CREATE TABLE IF NOT EXISTS checkpoint_service_pages(match BLOB NOT NULL,id BLOB NOT NULL,ordinal INTEGER NOT NULL,data BLOB NOT NULL CHECK(length(data)<=65536),digest BLOB NOT NULL CHECK(length(digest)=32),PRIMARY KEY(match,id,ordinal),FOREIGN KEY(match,id) REFERENCES checkpoint_service(match,id)) WITHOUT ROWID;");
}
template<class Store> Result<ServiceCapture> service_capture_row(Store& s,CheckpointTicket ticket) noexcept {
    if(ticket.match!=s.config.match||!ticket.id||!ticket.generation)return fail(Error::InvalidArgument);
    Statement row;if(auto r=row.prepare(s.database,"SELECT generation,revision,boot_term,boot_nonce,table_index,cursor,bytes,pages,complete,root,cache_order FROM checkpoint_service WHERE match=?1 AND id=?2");!r)return fail(r.error());
    if(auto r=row.number(1,ticket.match);!r)return fail(r.error());if(auto r=row.number(2,ticket.id);!r)return fail(r.error());auto code=sqlite3_step(row.value);if(code==SQLITE_DONE)return fail(Error::NotReady);if(code!=SQLITE_ROW)return fail(sql_error(code));
    auto gen=row.number(0),rev=row.number(1),term=row.number(2),bytes=row.number(6),order=row.number(10);if(!gen||!rev||!term||!bytes||!order)return fail(Error::RecoveryUnavailable);if(*gen!=ticket.generation)return fail(Error::StaleGeneration);
    auto nonce=sqlite_blob(row.value,3,32,32),cursor=sqlite_blob(row.value,5,0,service_cursor_bytes),root=sqlite_blob(row.value,9,32,32);if(!nonce||!cursor||!root)return fail(Error::RecoveryUnavailable);
    for(int col:{4,7,8})if(sqlite3_column_type(row.value,col)!=SQLITE_INTEGER)return fail(Error::RecoveryUnavailable);
    auto table=checkpoint_integer(row.value,4,service_tables.size()),pages=checkpoint_integer(row.value,7,checkpoint_maximum_chunks),complete=checkpoint_integer(row.value,8,1);
    if(!table||!pages||!complete||*bytes>checkpoint_maximum_bytes)return fail(Error::RecoveryUnavailable);
    if(bool(*complete)!=(*table==service_tables.size()))return fail(Error::RecoveryUnavailable);
    ServiceCapture result;result.progress={ticket,*rev,*bytes,*order,static_cast<std::uint32_t>(*pages),static_cast<std::uint32_t>(*table),bool(*complete),{}};result.boot.term=*term;
    std::copy(nonce->begin(),nonce->end(),result.boot.nonce.begin());std::copy(root->begin(),root->end(),result.progress.commitment.begin());std::copy(cursor->begin(),cursor->end(),result.cursor.begin());result.cursor_size=cursor->size();return result;
}
template<class Store> Status service_capture_guard(Store& s,const CheckpointContentReceipt& content,const ServiceCapture& capture) noexcept {
    if(!s.bound_boot||*s.bound_boot!=capture.boot)return fail(Error::StaleEpoch);if(auto r=checkpoint_anchor(s,content.manifest);!r)return r;
    auto revision=stored_service_revision(s.database,s.config.match);if(!revision)return fail(revision.error());if(*revision!=capture.progress.revision)return fail(Error::StaleGeneration);return {};
}
Result<bool> service_table_exists(sqlite3* db,const char* name) noexcept {
    Statement statement;if(auto r=statement.prepare(db,"SELECT type FROM sqlite_schema WHERE name=?1");!r)return fail(r.error());if(sqlite3_bind_text(statement.value,1,name,-1,SQLITE_STATIC)!=SQLITE_OK)return fail(Error::Io);auto code=sqlite3_step(statement.value);if(code==SQLITE_DONE)return false;if(code!=SQLITE_ROW)return fail(sql_error(code));auto type=sqlite_text(statement.value,0,16);if(!type||*type!="table")return fail(Error::RecoveryUnavailable);return true;
}
Status encode_service_cell(sqlite3_stmt* row,int column,int bound,bool nullable,Writer& out) noexcept {
    const auto type=sqlite3_column_type(row,column);
    if(type==SQLITE_NULL&&nullable){if(auto r=out.u64(0);!r)return r;return out.u64(0);}
    if(bound==0){if(type!=SQLITE_INTEGER)return fail(Error::RecoveryUnavailable);auto value=sqlite3_column_int64(row,column);if(value<0)return fail(Error::RecoveryUnavailable);if(auto r=out.u64(2);!r)return r;if(auto r=out.u64(8);!r)return r;return out.u64(static_cast<std::uint64_t>(value));}
    const auto maximum=static_cast<std::size_t>(bound<0?-bound:bound);auto blob=sqlite_blob(row,column,bound<0?0:maximum,maximum);if(!blob)return fail(blob.error());if(auto r=out.u64(1);!r)return r;if(auto r=out.u64(blob->size());!r)return r;return out.raw(*blob);
}
Result<std::uint64_t> service_number(sqlite3_stmt* row,int column) noexcept {
    auto bytes=sqlite_blob(row,column,8,8);if(!bytes)return fail(bytes.error());Reader reader(*bytes);return reader.u64();
}
Status validate_service_row(sqlite3_stmt* row,std::uint32_t table,std::uint64_t match) noexcept {
    if(table<=2){auto kind=service_number(row,0),epoch=service_number(row,2);if(!kind||*kind>2||!epoch||!*epoch)return fail(Error::RecoveryUnavailable);}
    if(table==0){auto first=service_number(row,4);if(!first)return fail(first.error());std::optional<std::uint64_t> through,issued;
        if(sqlite3_column_type(row,5)!=SQLITE_NULL){auto n=service_number(row,5);if(!n)return fail(n.error());through=*n;}
        if(sqlite3_column_type(row,6)!=SQLITE_NULL){auto n=service_number(row,6);if(!n)return fail(n.error());issued=*n;}
        if((through&&(!issued||*through>*issued||*through<*first))||(issued&&*issued<*first))return fail(Error::RecoveryUnavailable);
        auto bits=service_number(row,7);if(!bits)return fail(bits.error());
        if(!issued){if(*bits)return fail(Error::RecoveryUnavailable);}
        else if(through&&*through==UINT64_MAX){if(*bits)return fail(Error::RecoveryUnavailable);}
        else{const auto base=through?*through+1:*first;
            if(*bits&1)return fail(Error::RecoveryUnavailable);
            if(*issued<base){if(*bits)return fail(Error::RecoveryUnavailable);}
            else{const auto distance=*issued-base;if(distance>=64||(distance<63&&(*bits>>(distance+1))))return fail(Error::RecoveryUnavailable);}
        }
    }
    if(table==1){
        auto kind=service_number(row,0),actor=service_number(row,1),epoch=service_number(row,2),sequence=service_number(row,3);if(!kind||!actor||!epoch||!sequence)return fail(Error::RecoveryUnavailable);
        Statement parent;if(auto r=parent.prepare(sqlite3_db_handle(row),"SELECT epoch,first_seq,through,issued,bits FROM operation_actors WHERE match=?1 AND kind=?2 AND actor=?3");!r)return r;
        if(!parent.number(1,match)||!parent.number(2,*kind)||!parent.number(3,*actor))return fail(Error::Io);if(sqlite3_step(parent.value)!=SQLITE_ROW)return fail(Error::RecoveryUnavailable);
        auto current=parent.number(0),first=parent.number(1),issued=parent.number(3),bits=parent.number(4);if(!current||*current!=*epoch||!first||!issued||!bits)return fail(Error::RecoveryUnavailable);
        std::uint64_t base=*first;if(sqlite3_column_type(parent.value,2)!=SQLITE_NULL){auto through=parent.number(2);if(!through||*through==UINT64_MAX)return fail(Error::RecoveryUnavailable);base=*through+1;}
        if(*sequence<base||*sequence>*issued||*sequence-base>=64||(*bits&(std::uint64_t{1}<<(*sequence-base))))return fail(Error::RecoveryUnavailable);
    }
    if(table==2){auto token=service_number(row,6);if(!token||!*token)return fail(Error::RecoveryUnavailable);Statement order;if(auto r=order.prepare(sqlite3_db_handle(row),"SELECT cache_order FROM operation_settings WHERE id=1");!r)return r;if(sqlite3_step(order.value)!=SQLITE_ROW)return fail(Error::RecoveryUnavailable);auto high=order.number(0);if(!high||*token>*high)return fail(Error::RecoveryUnavailable);}

    if(table==3){if(sqlite3_column_type(row,1)!=SQLITE_INTEGER||sqlite3_column_int64(row,1)<0||sqlite3_column_int64(row,1)>=64)return fail(Error::RecoveryUnavailable);auto sequence=service_number(row,2);if(!sequence||!*sequence)return fail(Error::RecoveryUnavailable);}
    if(table==5||table==6){auto bytes=sqlite_blob(row,table==5?2:1,policy_bytes,policy_bytes);if(!bytes)return fail(bytes.error());auto policy=decode_policy(*bytes);if(!policy||policy->receipt.change.policy.match!=match)return fail(Error::RecoveryUnavailable);
        auto first=service_number(row,0);if(!first)return fail(first.error());if(table==5){auto second=service_number(row,1);if(!second||*first!=policy->receipt.change.policy.session||*second!=policy->receipt.change.policy.principal)return fail(Error::RecoveryUnavailable);}else if(*first!=policy->receipt.change.transition_id)return fail(Error::RecoveryUnavailable);
    }
    if(table==7){auto bytes=sqlite_blob(row,0,execution_bytes,execution_bytes);if(!bytes)return fail(bytes.error());auto execution=decode_execution(*bytes);if(!execution)return fail(execution.error());}
    if(table==8){auto bytes=sqlite_blob(row,2,authority_record_size,authority_record_size);if(!bytes)return fail(bytes.error());auto authority=decode_authority(*bytes);if(!authority)return fail(authority.error());auto sequence=service_number(row,0);if(!sequence||*sequence!=authority->proposal.grant.grant_sequence)return fail(Error::RecoveryUnavailable);}
    if(table==4){auto bytes=sqlite_blob(row,1,binding_bytes,binding_bytes);if(!bytes)return fail(bytes.error());Reader reader(*bytes);auto boot=read_boot(reader);auto clock=reader.raw(32);auto policy=read_policy(reader);auto revision=reader.u64();std::array<std::uint64_t,8> grant{};for(auto& n:grant){auto value=reader.u64();if(!value)return fail(value.error());n=*value;}auto kind=reader.raw(1),scope=reader.raw(32);if(!boot||!clock||!policy||policy->match!=match||!revision||!kind||static_cast<unsigned>((*kind)[0])>1||!scope||!reader.empty()||!grant[4]||!grant[6]||grant[6]>2000000)return fail(Error::RecoveryUnavailable);}
    return {};
}
Status bind_service_cursor(Statement& statement,const ServiceTable& table,std::span<const std::byte> bytes) noexcept {
    Reader reader(bytes);for(unsigned i=0;i<table.key_count;++i){auto tag=reader.u64(),length=reader.u64();if(!tag||!length)return fail(Error::RecoveryUnavailable);if(*tag==1&&*length==8){auto data=reader.raw(8);if(!data)return fail(data.error());if(auto r=statement.bytes(static_cast<int>(i)+2,*data);!r)return r;}else if(*tag==2&&*length==8){auto value=reader.u64();if(!value||*value>INT64_MAX)return fail(Error::RecoveryUnavailable);if(sqlite3_bind_int64(statement.value,static_cast<int>(i)+2,static_cast<sqlite3_int64>(*value))!=SQLITE_OK)return fail(Error::Io);}else return fail(Error::RecoveryUnavailable);}
    return reader.empty()?Status{}:Status(fail(Error::RecoveryUnavailable));
}
struct ServicePage {
    std::array<std::byte,checkpoint_chunk_bytes> bytes{};std::size_t size{};
    std::array<std::byte,service_cursor_bytes> cursor{};std::size_t cursor_size{};bool table_done{};
};
Result<ServicePage> encode_service_page(sqlite3* db,std::uint64_t match,const ServiceCapture& capture) noexcept {
    if(capture.progress.next_table>=service_tables.size())return fail(Error::NotReady);const auto& table=service_tables[capture.progress.next_table];
    ServicePage result;Writer writer(result.bytes);for(auto v:{std::uint64_t{1},std::uint64_t(capture.progress.next_table),std::uint64_t{0},std::uint64_t{0}})if(auto r=writer.u64(v);!r)return fail(r.error());
    auto exists=service_table_exists(db,table.name);if(!exists)return fail(exists.error());std::uint64_t rows=0;
    if(!*exists){if(capture.progress.next_table<4)return fail(Error::RecoveryUnavailable);result.table_done=true;}else{
        std::array<char,1024> query{},predicate{},parameters{};
        if(capture.cursor_size){if(!table.key_count)return fail(Error::RecoveryUnavailable);std::size_t used=0;for(unsigned i=0;i<table.key_count;++i){auto n=std::snprintf(parameters.data()+used,parameters.size()-used,"%s?%u",i?",":"",i+2);if(n<0||static_cast<std::size_t>(n)>=parameters.size()-used)return fail(Error::CapacityExceeded);used+=static_cast<std::size_t>(n);}auto n=std::snprintf(predicate.data(),predicate.size()," AND (%s)>(%s)",table.keys,parameters.data());if(n<0||static_cast<std::size_t>(n)>=predicate.size())return fail(Error::CapacityExceeded);}
        auto n=std::snprintf(query.data(),query.size(),"SELECT %s FROM %s WHERE match=?1%s%s%s LIMIT 64",table.columns,table.name,predicate.data(),table.key_count?" ORDER BY ":"",table.keys);if(n<0||static_cast<std::size_t>(n)>=query.size())return fail(Error::CapacityExceeded);
        Statement statement;if(auto r=statement.prepare(db,query.data());!r)return fail(r.error());if(auto r=statement.number(1,match);!r)return fail(r.error());if(capture.cursor_size)if(auto r=bind_service_cursor(statement,table,{capture.cursor.data(),capture.cursor_size});!r)return fail(r.error());
        for(;;){auto code=sqlite3_step(statement.value);if(code==SQLITE_DONE){result.table_done=true;break;}if(code!=SQLITE_ROW)return fail(sql_error(code));if(sqlite3_column_count(statement.value)!=table.count)return fail(Error::RecoveryUnavailable);if(auto r=validate_service_row(statement.value,capture.progress.next_table,match);!r)return fail(r.error());
            std::array<std::byte,16*1024> encoded{};Writer row(encoded);if(auto r=row.u64(table.count);!r)return fail(r.error());std::array<std::byte,service_cursor_bytes> key{};Writer key_writer(key);
            for(unsigned col=0;col<table.count;++col){bool nullable=capture.progress.next_table==0&&(col==5||col==6);if(auto r=encode_service_cell(statement.value,col,table.bounds[col],nullable,row);!r)return fail(r.error());if(col<table.key_count)if(auto r=encode_service_cell(statement.value,col,table.bounds[col],false,key_writer);!r)return fail(r.error());}
            if(writer.size()+8+row.size()>result.bytes.size()){if(!rows)return fail(Error::CapacityExceeded);break;}
            if(auto r=writer.u64(row.size());!r)return fail(r.error());if(auto r=writer.raw(std::span(encoded).first(row.size()));!r)return fail(r.error());++rows;result.cursor=key;result.cursor_size=key_writer.size();
            if(!table.key_count){if(sqlite3_step(statement.value)!=SQLITE_DONE)return fail(Error::RecoveryUnavailable);result.table_done=true;break;}
            if(rows==64)break;
        }
    }
    Writer header(std::span(result.bytes).first(32));for(auto v:{std::uint64_t{1},std::uint64_t(capture.progress.next_table),rows,std::uint64_t(result.table_done)})if(auto r=header.u64(v);!r)return fail(r.error());result.size=writer.size();return result;
}
}
Result<Fingerprint> checkpoint_service_codec(CryptographicDigest& digest) noexcept {
    if(digest.algorithm()!=DigestAlgorithm::Sha256)return fail(Error::Unsupported);
    constexpr char descriptor[]="superpos.service-snapshot.v1:actor8,pending6,result7,outbox5,control-commit3,policy3,transition2,lease2,authority4;u64be;tagged-cells;indexed-keys;historical-authority";
    Fingerprint result{};if(auto r=digest.hash({reinterpret_cast<const std::byte*>(descriptor),sizeof(descriptor)-1},result);!r)return fail(r.error());return result;
}
Result<CheckpointServiceProgress> SqliteJournal::begin_checkpoint_service(CheckpointTicket ticket,bool restart) noexcept {
    if(!impl_||!impl_->own_thread())return fail(Error::PermissionDenied);auto& s=*impl_;if(!s.checkpoint_digest||!s.bound_boot)return fail(Error::NotReady);Transaction tx{s.database};if(auto r=tx.begin();!r)return fail(r.error());auto image=checkpoint_row(s,ticket.id);if(!image)return fail(image.error());if(image->ticket!=ticket)return fail(Error::StaleGeneration);if(image->state==CheckpointContentState::Staging)return fail(Error::NotReady);if(image->state==CheckpointContentState::RecoveryVerified)return fail(Error::PermissionDenied);if(auto r=checkpoint_anchor(s,image->manifest);!r)return fail(r.error());if(auto r=service_capture_tables(s);!r)return fail(r.error());
    Result<Fingerprint> codec=fail(Error::NotReady);{CheckpointCallbackGuard guard(s.checkpoint_callback);codec=checkpoint_service_codec(*s.checkpoint_digest);}if(!codec)return fail(codec.error());if(*codec!=image->manifest.service_codec)return fail(Error::IncompatibleSchema);
    auto old=service_capture_row(s,ticket);if(old&&!restart){if(auto r=service_capture_guard(s,*image,*old);!r)return fail(r.error());if(auto r=s.reconfirm();!r)return fail(r.error());if(auto r=tx.commit();!r)return fail(r.error());return old->progress;}if(!old&&old.error()!=Error::NotReady)return fail(old.error());
    if(old){for(const auto* sql:{"DELETE FROM checkpoint_service_pages WHERE match=?1 AND id=?2","DELETE FROM checkpoint_service WHERE match=?1 AND id=?2"}){Statement remove;if(auto r=remove.prepare(s.database,sql);!r)return fail(r.error());if(auto r=remove.number(1,ticket.match);!r)return fail(r.error());if(auto r=remove.number(2,ticket.id);!r)return fail(r.error());if(sqlite3_step(remove.value)!=SQLITE_DONE)return fail(Error::Io);}}
    auto revision=stored_service_revision(s.database,s.config.match);if(!revision)return fail(revision.error());std::array<std::byte,128> header{};Writer writer(header);for(auto v:{std::uint64_t{0x5350535356430001},ticket.match,ticket.id,ticket.generation,*revision,s.bound_boot->term,image->manifest.epoch,image->manifest.sequence,image->manifest.tick})if(auto r=writer.u64(v);!r)return fail(r.error());if(auto r=writer.raw(s.bound_boot->nonce);!r)return fail(r.error());auto initial=checkpoint_hash(s,std::span(header).first(writer.size()));if(!initial)return fail(initial.error());
    Statement insert;if(auto r=insert.prepare(s.database,"INSERT INTO checkpoint_service VALUES(?1,?2,?3,?4,?5,?6,0,?7,?8,0,0,?9,?10)");!r)return fail(r.error());
    for(auto [column,value]:{std::pair{1,ticket.match},{2,ticket.id},{3,ticket.generation},{4,*revision},{5,s.bound_boot->term},{8,std::uint64_t{0}},{10,std::uint64_t{0}}})if(auto r=insert.number(column,value);!r)return fail(r.error());
    if(auto r=insert.bytes(6,s.bound_boot->nonce);!r)return fail(r.error());if(auto r=insert.bytes(7,{});!r)return fail(r.error());if(auto r=insert.bytes(9,*initial);!r)return fail(r.error());if(sqlite3_step(insert.value)!=SQLITE_DONE)return fail(Error::Io);if(auto r=tx.commit();!r)return fail(r.error());return CheckpointServiceProgress{ticket,*revision,0,0,0,0,false,*initial};
}
Result<CheckpointServiceProgress> SqliteJournal::capture_checkpoint_service_page(CheckpointTicket ticket,std::uint32_t expected_page) noexcept {
    if(!impl_||!impl_->own_thread())return fail(Error::PermissionDenied);auto& s=*impl_;if(!s.checkpoint_digest)return fail(Error::NotReady);Transaction tx{s.database};if(auto r=tx.begin();!r)return fail(r.error());auto image=checkpoint_row(s,ticket.id);if(!image)return fail(image.error());if(image->ticket!=ticket)return fail(Error::StaleGeneration);auto capture=service_capture_row(s,ticket);if(!capture)return fail(capture.error());if(auto r=service_capture_guard(s,*image,*capture);!r)return fail(r.error());
    if(expected_page>capture->progress.pages||expected_page+std::uint64_t{1}<capture->progress.pages)return fail(Error::StaleGeneration);
    if(expected_page<capture->progress.pages||capture->progress.complete){if(auto r=s.reconfirm();!r)return fail(r.error());if(auto r=tx.commit();!r)return fail(r.error());return capture->progress;}
    auto page=encode_service_page(s.database,ticket.match,*capture);if(!page)return fail(page.error());auto& progress=capture->progress;
    if(progress.pages>=checkpoint_maximum_chunks||progress.bytes>checkpoint_maximum_bytes-image->manifest.bytes||page->size>checkpoint_maximum_bytes-image->manifest.bytes-progress.bytes)return fail(Error::CapacityExceeded);
    auto hash=checkpoint_hash(s,std::span(page->bytes).first(page->size));if(!hash)return fail(hash.error());
    Statement insert;if(auto r=insert.prepare(s.database,"INSERT INTO checkpoint_service_pages VALUES(?1,?2,?3,?4,?5)");!r)return fail(r.error());if(auto r=insert.number(1,ticket.match);!r)return fail(r.error());if(auto r=insert.number(2,ticket.id);!r)return fail(r.error());if(sqlite3_bind_int(insert.value,3,static_cast<int>(progress.pages))!=SQLITE_OK)return fail(Error::Io);if(auto r=insert.bytes(4,std::span(page->bytes).first(page->size));!r)return fail(r.error());if(auto r=insert.bytes(5,*hash);!r)return fail(r.error());if(sqlite3_step(insert.value)!=SQLITE_DONE)return fail(Error::Io);
    std::array<std::byte,80> chain{};Writer writer(chain);if(auto r=writer.raw(progress.commitment);!r)return fail(r.error());if(auto r=writer.u64(progress.pages);!r)return fail(r.error());if(auto r=writer.raw(*hash);!r)return fail(r.error());auto root=checkpoint_hash(s,std::span(chain).first(writer.size()));if(!root)return fail(root.error());progress.commitment=*root;++progress.pages;progress.bytes+=page->size;if(page->table_done)++progress.next_table;progress.complete=progress.next_table==service_tables.size();
    if(progress.complete){Statement order;if(auto r=order.prepare(s.database,"SELECT cache_order FROM operation_settings WHERE id=1");!r)return fail(r.error());if(sqlite3_step(order.value)!=SQLITE_ROW)return fail(Error::RecoveryUnavailable);auto value=order.number(0);if(!value)return fail(value.error());progress.cache_order=*value;
        std::array<std::byte,96> terminal{};Writer final(terminal);if(auto r=final.raw(progress.commitment);!r)return fail(r.error());for(auto value:{progress.bytes,std::uint64_t(progress.pages),progress.cache_order,progress.revision})if(auto r=final.u64(value);!r)return fail(r.error());if(auto r=final.raw(image->manifest.content);!r)return fail(r.error());auto committed=checkpoint_hash(s,std::span(terminal).first(final.size()));if(!committed)return fail(committed.error());progress.commitment=*committed;
    }
    Statement update;if(auto r=update.prepare(s.database,"UPDATE checkpoint_service SET table_index=?3,cursor=?4,bytes=?5,pages=?6,complete=?7,root=?8,cache_order=?9 WHERE match=?1 AND id=?2");!r)return fail(r.error());if(auto r=update.number(1,ticket.match);!r)return fail(r.error());if(auto r=update.number(2,ticket.id);!r)return fail(r.error());if(sqlite3_bind_int(update.value,3,static_cast<int>(progress.next_table))!=SQLITE_OK)return fail(Error::Io);if(auto r=update.bytes(4,page->table_done?std::span<const std::byte>{}:std::span<const std::byte>(page->cursor).first(page->cursor_size));!r)return fail(r.error());if(auto r=update.number(5,progress.bytes);!r)return fail(r.error());if(sqlite3_bind_int(update.value,6,static_cast<int>(progress.pages))!=SQLITE_OK||sqlite3_bind_int(update.value,7,progress.complete?1:0)!=SQLITE_OK)return fail(Error::Io);if(auto r=update.bytes(8,progress.commitment);!r)return fail(r.error());if(auto r=update.number(9,progress.cache_order);!r)return fail(r.error());if(sqlite3_step(update.value)!=SQLITE_DONE)return fail(Error::Io);if(auto r=service_capture_guard(s,*image,*capture);!r)return fail(r.error());if(auto r=tx.commit();!r)return fail(r.error());return progress;
}
Result<CheckpointServiceProgress> SqliteJournal::checkpoint_service_progress(CheckpointTicket ticket) noexcept {
    if(!impl_||!impl_->own_thread())return fail(Error::PermissionDenied);Transaction tx{impl_->database};if(auto r=tx.begin();!r)return fail(r.error());auto capture=service_capture_row(*impl_,ticket);if(!capture)return fail(capture.error());if(auto r=impl_->reconfirm();!r)return fail(r.error());if(auto r=tx.commit();!r)return fail(r.error());return capture->progress;
}
namespace {
template<class Store> Result<std::size_t> read_service_page(Store& s,CheckpointTicket ticket,std::uint32_t index,std::span<std::byte> output) noexcept {
    Statement row;if(auto r=row.prepare(s.database,"SELECT data,digest FROM checkpoint_service_pages WHERE match=?1 AND id=?2 AND ordinal=?3");!r)return fail(r.error());if(auto r=row.number(1,ticket.match);!r)return fail(r.error());if(auto r=row.number(2,ticket.id);!r)return fail(r.error());if(sqlite3_bind_int(row.value,3,static_cast<int>(index))!=SQLITE_OK)return fail(Error::Io);auto code=sqlite3_step(row.value);if(code==SQLITE_DONE)return fail(Error::NotReady);if(code!=SQLITE_ROW)return fail(sql_error(code));auto bytes=sqlite_blob(row.value,0,32,checkpoint_chunk_bytes),hash=sqlite_blob(row.value,1,32,32);if(!bytes||!hash)return fail(Error::RecoveryUnavailable);if(output.size()<bytes->size())return fail(Error::Truncated);Fingerprint actual{};if(auto r=s.checkpoint_digest->hash(*bytes,actual);!r)return fail(r.error());if(!std::equal(actual.begin(),actual.end(),hash->begin()))return fail(Error::RecoveryUnavailable);std::copy(bytes->begin(),bytes->end(),output.begin());return bytes->size();
}
}
Result<std::size_t> SqliteJournal::read_checkpoint_service_page(CheckpointTicket ticket,std::uint32_t index,std::span<std::byte> output) noexcept {
    if(!impl_||!impl_->own_thread())return fail(Error::PermissionDenied);auto& s=*impl_;if(!s.checkpoint_digest)return fail(Error::NotReady);Transaction tx{s.database};if(auto r=tx.begin();!r)return fail(r.error());auto capture=service_capture_row(s,ticket);if(!capture)return fail(capture.error());if(!capture->progress.complete||index>=capture->progress.pages)return fail(Error::NotReady);std::array<std::byte,checkpoint_chunk_bytes> buffer{};Result<std::size_t> size=fail(Error::NotReady);{CheckpointCallbackGuard guard(s.checkpoint_callback);size=read_service_page(s,ticket,index,buffer);}if(!size)return fail(size.error());if(output.size()<*size)return fail(Error::Truncated);if(auto r=s.reconfirm();!r)return fail(r.error());if(auto r=tx.commit();!r)return fail(r.error());std::copy_n(buffer.begin(),*size,output.begin());return size;
}
namespace {
template<class Store> Status verify_checkpoint_image(Store& s,CheckpointContentReceipt& value,ServiceCapture& cut,CheckpointParticipantVerifier& verifier) noexcept {
    auto* image=&value;auto* capture=&cut;const auto ticket=image->ticket;
    // Re-read every stored byte under the same cut; corrupted data cannot be
    // promoted merely because a digest row survived. Participant semantics are
    // additionally checked by the explicitly trusted native verifier below.
    std::array<std::byte,checkpoint_chunk_bytes> buffer{};
    {CheckpointCallbackGuard guard(s.checkpoint_callback);
        std::array<Fingerprint,checkpoint_maximum_chunks> participant_hashes{};
        for(std::uint32_t i=0;i<checkpoint_chunks(image->manifest);++i){auto size=checkpoint_read_data(s,*image,i,buffer);if(!size)return fail(size.error());if(auto r=s.checkpoint_digest->hash(std::span(buffer).first(*size),participant_hashes[i]);!r)return fail(r.error());}
        auto content=checkpoint_content_commitment(image->manifest,std::span(participant_hashes).first(checkpoint_chunks(image->manifest)),*s.checkpoint_digest);if(!content)return fail(content.error());if(*content!=image->manifest.content)return fail(Error::RecoveryUnavailable);
        auto codec=checkpoint_service_codec(*s.checkpoint_digest);if(!codec)return fail(codec.error());if(*codec!=image->manifest.service_codec)return fail(Error::IncompatibleSchema);
        // Regenerate every canonical page from the same revision under this
        // write transaction. This checks exact table coverage, key ordering,
        // null/width rules and every stored service value, not only its digest.
        ServiceCapture expected=*capture;expected.progress.next_table=0;expected.cursor_size=0;
        std::array<std::byte,128> seed{};Writer seed_writer(seed);for(auto value:{std::uint64_t{0x5350535356430001},ticket.match,ticket.id,ticket.generation,capture->progress.revision,capture->boot.term,image->manifest.epoch,image->manifest.sequence,image->manifest.tick})if(auto r=seed_writer.u64(value);!r)return fail(r.error());if(auto r=seed_writer.raw(capture->boot.nonce);!r)return fail(r.error());Fingerprint chain{};if(auto r=s.checkpoint_digest->hash(std::span(seed).first(seed_writer.size()),chain);!r)return fail(r.error());std::uint64_t total=0;
        for(std::uint32_t i=0;i<capture->progress.pages;++i){
            auto page=encode_service_page(s.database,ticket.match,expected);if(!page)return fail(page.error());auto stored=read_service_page(s,ticket,i,buffer);if(!stored)return fail(stored.error());if(*stored!=page->size||!std::equal(page->bytes.begin(),page->bytes.begin()+page->size,buffer.begin()))return fail(Error::RecoveryUnavailable);
            Fingerprint hash{};if(auto r=s.checkpoint_digest->hash(std::span(buffer).first(*stored),hash);!r)return fail(r.error());std::array<std::byte,72> link{};Writer link_writer(link);if(auto r=link_writer.raw(chain);!r)return fail(r.error());if(auto r=link_writer.u64(i);!r)return fail(r.error());if(auto r=link_writer.raw(hash);!r)return fail(r.error());if(auto r=s.checkpoint_digest->hash(link,chain);!r)return fail(r.error());total+=page->size;
            if(page->table_done){++expected.progress.next_table;expected.cursor_size=0;}else{expected.cursor=page->cursor;expected.cursor_size=page->cursor_size;}
        }
        if(expected.progress.next_table!=service_tables.size()||total!=capture->progress.bytes)return fail(Error::RecoveryUnavailable);
        std::array<std::byte,96> terminal{};Writer final(terminal);if(auto r=final.raw(chain);!r)return fail(r.error());for(auto value:{total,std::uint64_t(capture->progress.pages),capture->progress.cache_order,capture->progress.revision})if(auto r=final.u64(value);!r)return fail(r.error());if(auto r=final.raw(image->manifest.content);!r)return fail(r.error());if(auto r=s.checkpoint_digest->hash(terminal,chain);!r)return fail(r.error());if(chain!=capture->progress.commitment)return fail(Error::RecoveryUnavailable);
        struct Reader final:CheckpointContentReader {Store& store;const CheckpointContentReceipt& image;Reader(Store& s,const CheckpointContentReceipt& r):store(s),image(r){}Result<std::size_t> read(std::uint32_t index,std::span<std::byte> output) noexcept override{return checkpoint_read_data(store,image,index,output);}} reader{s,*image};
        if(verifier.participant_fingerprint()!=image->manifest.participants)return fail(Error::IncompatibleSchema);if(unsigned(verifier.supported_grade())<unsigned(image->manifest.grade)||unsigned(verifier.supported_grade())>3)return fail(Error::Unsupported);if(auto r=verifier.verify(image->manifest,reader);!r)return fail(r.error());if(verifier.participant_fingerprint()!=image->manifest.participants)return fail(Error::IncompatibleSchema);if(unsigned(verifier.supported_grade())<unsigned(image->manifest.grade)||unsigned(verifier.supported_grade())>3)return fail(Error::Unsupported);
    }
    return {};
}
}
Result<CheckpointRecoveryReceipt> SqliteJournal::verify_checkpoint_recovery(CheckpointTicket ticket,CheckpointParticipantVerifier& verifier) noexcept {
    if(!impl_||!impl_->own_thread())return fail(Error::PermissionDenied);auto& s=*impl_;if(!s.checkpoint_digest)return fail(Error::NotReady);Transaction tx{s.database};if(auto r=tx.begin();!r)return fail(r.error());auto image=checkpoint_row(s,ticket.id);if(!image)return fail(image.error());if(image->ticket!=ticket)return fail(Error::StaleGeneration);if(image->state==CheckpointContentState::Staging)return fail(Error::NotReady);auto capture=service_capture_row(s,ticket);if(!capture)return fail(capture.error());if(!capture->progress.complete)return fail(Error::NotReady);if(auto r=service_capture_guard(s,*image,*capture);!r)return fail(r.error());
    if(auto r=verify_checkpoint_image(s,*image,*capture,verifier);!r)return fail(r.error());
    if(image->state!=CheckpointContentState::RecoveryVerified){Statement count;if(auto r=count.prepare(s.database,"SELECT COUNT(*) FROM checkpoint_images WHERE match=?1 AND state=2");!r)return fail(r.error());if(auto r=count.number(1,ticket.match);!r)return fail(r.error());if(sqlite3_step(count.value)!=SQLITE_ROW)return fail(Error::Io);auto retained=checkpoint_integer(count.value,0,2);if(!retained)return fail(retained.error());if(*retained>=2)return fail(Error::CapacityExceeded);}
    if(auto r=service_capture_guard(s,*image,*capture);!r)return fail(r.error());Statement promote;if(auto r=promote.prepare(s.database,"UPDATE checkpoint_images SET state=2 WHERE match=?1 AND id=?2");!r)return fail(r.error());if(auto r=promote.number(1,ticket.match);!r)return fail(r.error());if(auto r=promote.number(2,ticket.id);!r)return fail(r.error());if(sqlite3_step(promote.value)!=SQLITE_DONE)return fail(Error::Io);if(auto r=s.reconfirm();!r)return fail(r.error());if(auto r=tx.commit();!r)return fail(r.error());image->state=CheckpointContentState::RecoveryVerified;return CheckpointRecoveryReceipt{*image,capture->progress,{capture->boot.term,capture->boot.nonce}};
}
