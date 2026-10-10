#include "bounded_vfs.hpp"
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <new>

namespace superpos::detail {
namespace {
struct WrappedFile {
    sqlite3_file file{};
    sqlite3_file* inner{};
    std::uint64_t ceiling{};
    bool wal{};
    StorageFaultInjector* fault_injector{};
};
constexpr auto inner_offset=(sizeof(WrappedFile)+alignof(std::max_align_t)-1)/alignof(std::max_align_t)*alignof(std::max_align_t);
WrappedFile* file(sqlite3_file* f) noexcept { return reinterpret_cast<WrappedFile*>(f); }
sqlite3_file* inner(sqlite3_file* f) noexcept { return file(f)->inner; }
const sqlite3_io_methods* methods(sqlite3_file* f) noexcept { return inner(f)->pMethods; }
int close(sqlite3_file* f) { auto result=methods(f)->xClose(inner(f)); f->pMethods=nullptr; return result; }
int read(sqlite3_file* f,void* data,int size,sqlite3_int64 offset) { return methods(f)->xRead(inner(f),data,size,offset); }
int write(sqlite3_file* f,const void* data,int size,sqlite3_int64 offset) {
    if (size<0||offset<0) return SQLITE_IOERR_WRITE;
    if(file(f)->fault_injector && file(f)->fault_injector->reject(StorageIo::Write,file(f)->wal)) return SQLITE_IOERR_WRITE;
    if (file(f)->wal && (static_cast<std::uint64_t>(offset)>file(f)->ceiling ||
        static_cast<std::uint64_t>(size)>file(f)->ceiling-static_cast<std::uint64_t>(offset))) return SQLITE_FULL;
    return methods(f)->xWrite(inner(f),data,size,offset);
}
int truncate(sqlite3_file* f,sqlite3_int64 size) {
    if (size<0) return SQLITE_IOERR_TRUNCATE;
    if(file(f)->fault_injector && file(f)->fault_injector->reject(StorageIo::Truncate,file(f)->wal)) return SQLITE_IOERR_TRUNCATE;
    if (file(f)->wal && static_cast<std::uint64_t>(size)>file(f)->ceiling) return SQLITE_FULL;
    return methods(f)->xTruncate(inner(f),size);
}
int sync(sqlite3_file* f,int flags) {
    if(file(f)->fault_injector && file(f)->fault_injector->reject(StorageIo::Sync,file(f)->wal)) return SQLITE_IOERR_FSYNC;
    if(file(f)->fault_injector && file(f)->fault_injector->elide_sync(file(f)->wal)) return SQLITE_OK;
    return methods(f)->xSync(inner(f),flags);
}
int size(sqlite3_file* f,sqlite3_int64* size) { return methods(f)->xFileSize(inner(f),size); }
int lock(sqlite3_file* f,int value) { return methods(f)->xLock(inner(f),value); }
int unlock(sqlite3_file* f,int value) { return methods(f)->xUnlock(inner(f),value); }
int reserved(sqlite3_file* f,int* value) { return methods(f)->xCheckReservedLock(inner(f),value); }
int control(sqlite3_file* f,int op,void* value) {
    // Size hints are advisory; prevent the native VFS preallocating a WAL past
    // the bound even before the first actual page write.
    if (file(f)->wal && op==SQLITE_FCNTL_SIZE_HINT && value &&
        *static_cast<sqlite3_int64*>(value)>static_cast<sqlite3_int64>(file(f)->ceiling)) return SQLITE_FULL;
    return methods(f)->xFileControl(inner(f),op,value);
}
int sector(sqlite3_file* f) { return methods(f)->xSectorSize(inner(f)); }
int characteristics(sqlite3_file* f) { return methods(f)->xDeviceCharacteristics(inner(f)); }
int shm_map(sqlite3_file* f,int page,int page_size,int extend,void volatile** output) {
    return methods(f)->iVersion>=2 && methods(f)->xShmMap?methods(f)->xShmMap(inner(f),page,page_size,extend,output):SQLITE_IOERR_SHMMAP;
}
int shm_lock(sqlite3_file* f,int offset,int count,int flags) {
    return methods(f)->iVersion>=2 && methods(f)->xShmLock?methods(f)->xShmLock(inner(f),offset,count,flags):SQLITE_IOERR_SHMLOCK;
}
void shm_barrier(sqlite3_file* f) { if (methods(f)->iVersion>=2 && methods(f)->xShmBarrier) methods(f)->xShmBarrier(inner(f)); }
int shm_unmap(sqlite3_file* f,int remove) {
    return methods(f)->iVersion>=2 && methods(f)->xShmUnmap?methods(f)->xShmUnmap(inner(f),remove):SQLITE_OK;
}
int fetch(sqlite3_file* f,sqlite3_int64 offset,int amount,void** output) {
    *output=nullptr;
    return methods(f)->iVersion>=3 && methods(f)->xFetch?methods(f)->xFetch(inner(f),offset,amount,output):SQLITE_OK;
}
int unfetch(sqlite3_file* f,sqlite3_int64 offset,void* pointer) {
    return methods(f)->iVersion>=3 && methods(f)->xUnfetch?methods(f)->xUnfetch(inner(f),offset,pointer):SQLITE_OK;
}
const sqlite3_io_methods io{3,close,read,write,truncate,sync,size,lock,unlock,reserved,control,sector,characteristics,shm_map,shm_lock,shm_barrier,shm_unmap,fetch,unfetch};
BoundedVfs* state(sqlite3_vfs* v) { return static_cast<BoundedVfs*>(v->pAppData); }
sqlite3_vfs* source(sqlite3_vfs* v) { return state(v)->source; }
int open(sqlite3_vfs* v,const char* name,sqlite3_file* result,int flags,int* output) {
    auto* wrapped=new(result) WrappedFile;
    wrapped->inner=reinterpret_cast<sqlite3_file*>(reinterpret_cast<std::byte*>(result)+inner_offset);
    wrapped->inner->pMethods=nullptr;
    wrapped->wal=(flags&SQLITE_OPEN_WAL)!=0;
    wrapped->ceiling=state(v)->limit;
    wrapped->fault_injector=state(v)->fault_injector;
    auto* native=source(v);
    auto code=native->xOpen(native,name,wrapped->inner,flags,output);
    if (code!=SQLITE_OK) return code;
    if (wrapped->wal) {
        sqlite3_int64 bytes{};
        code=wrapped->inner->pMethods->xFileSize(wrapped->inner,&bytes);
        if (code!=SQLITE_OK||bytes<0||static_cast<std::uint64_t>(bytes)>wrapped->ceiling) {
            wrapped->inner->pMethods->xClose(wrapped->inner);
            return code==SQLITE_OK?SQLITE_FULL:code;
        }
    }
    result->pMethods=&io; return SQLITE_OK;
}
int remove(sqlite3_vfs* v,const char* name,int sync_dir) { auto* s=source(v); return s->xDelete(s,name,sync_dir); }
int access(sqlite3_vfs* v,const char* name,int flags,int* output) { auto* s=source(v); return s->xAccess(s,name,flags,output); }
int path(sqlite3_vfs* v,const char* name,int length,char* output) { auto* s=source(v); return s->xFullPathname(s,name,length,output); }
void* dl_open(sqlite3_vfs* v,const char* name) { auto* s=source(v); return s->xDlOpen?s->xDlOpen(s,name):nullptr; }
void dl_error(sqlite3_vfs* v,int length,char* output) { auto* s=source(v); if(s->xDlError) s->xDlError(s,length,output); else if(length) output[0]=0; }
void (*dl_symbol(sqlite3_vfs* v,void* handle,const char* name))(void) { auto* s=source(v); return s->xDlSym?s->xDlSym(s,handle,name):nullptr; }
void dl_close(sqlite3_vfs* v,void* handle) { auto* s=source(v); if(s->xDlClose) s->xDlClose(s,handle); }
int randomness(sqlite3_vfs* v,int count,char* output) { auto* s=source(v); return s->xRandomness(s,count,output); }
int sleep(sqlite3_vfs* v,int micros) { auto* s=source(v); return s->xSleep(s,micros); }
int time(sqlite3_vfs* v,double* output) { auto* s=source(v); return s->xCurrentTime(s,output); }
int error(sqlite3_vfs* v,int length,char* output) { auto* s=source(v); return s->xGetLastError?s->xGetLastError(s,length,output):0; }
int time64(sqlite3_vfs* v,sqlite3_int64* output) { auto* s=source(v); return s->iVersion>=2&&s->xCurrentTimeInt64?s->xCurrentTimeInt64(s,output):SQLITE_ERROR; }
int set_call(sqlite3_vfs* v,const char* name,sqlite3_syscall_ptr call) { auto* s=source(v); return s->iVersion>=3&&s->xSetSystemCall?s->xSetSystemCall(s,name,call):SQLITE_NOTFOUND; }
sqlite3_syscall_ptr get_call(sqlite3_vfs* v,const char* name) { auto* s=source(v); return s->iVersion>=3&&s->xGetSystemCall?s->xGetSystemCall(s,name):nullptr; }
const char* next_call(sqlite3_vfs* v,const char* name) { auto* s=source(v); return s->iVersion>=3&&s->xNextSystemCall?s->xNextSystemCall(s,name):nullptr; }
std::atomic<std::uint64_t> next_name{1};
}
Status BoundedVfs::initialize(std::uint64_t bytes) noexcept {
    source=sqlite3_vfs_find(nullptr);
    if (!source||source->iVersion<2||bytes>static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max())) return fail(Error::Unsupported);
    auto id=next_name.load();
    do {
        if (!id||id==std::numeric_limits<std::uint64_t>::max()) return fail(Error::CounterExhausted);
    } while (!next_name.compare_exchange_weak(id,id+1));
    std::snprintf(name,sizeof(name),"superpos-wal-%llu",static_cast<unsigned long long>(id));
    limit=bytes;
    table={3,static_cast<int>(inner_offset)+source->szOsFile,source->mxPathname,nullptr,name,this,open,remove,access,path,dl_open,dl_error,dl_symbol,dl_close,randomness,sleep,time,error,time64,set_call,get_call,next_call};
    const auto code=sqlite3_vfs_register(&table,0);
    if(code!=SQLITE_OK) return fail(Error::Io);
    registered=true; return {};
}
BoundedVfs::~BoundedVfs() { if(registered) sqlite3_vfs_unregister(&table); }
}
