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
 Impl(Clock& c,DatagramIO& d,DtlsConfig s) noexcept:clock(&c),io(&d),settings(s){mbedtls_ssl_init(&ssl);mbedtls_ssl_config_init(&config);mbedtls_ssl_cookie_init(&cookie);started=c.now_ms();}
 ~Impl(){mbedtls_ssl_free(&ssl);mbedtls_ssl_config_free(&config);mbedtls_ssl_cookie_free(&cookie);mbedtls_platform_zeroize(key.data(),key.size());mbedtls_platform_zeroize(pending.data(),pending.size());}
 static int write(void* p,const unsigned char* b,std::size_t n) noexcept {
  auto& self=*static_cast<Impl*>(p);if(!n||n>self.settings.udp_payload_ceiling)return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
  auto r=self.io->send({reinterpret_cast<const std::byte*>(b),n});
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
 Status flush() noexcept {if(!pending_size)return {};int n=mbedtls_ssl_write(&ssl,reinterpret_cast<const unsigned char*>(pending.data()),pending_size);if(n==MBEDTLS_ERR_SSL_WANT_READ||n==MBEDTLS_ERR_SSL_WANT_WRITE)return fail(Error::Busy);if(n<0||static_cast<std::size_t>(n)!=pending_size){failed=true;return fail(Error::AuthenticationFailed);}mbedtls_platform_zeroize(pending.data(),pending_size);pending_size=0;return {};}
};
DtlsAssociation::~DtlsAssociation(){if(impl_){impl_->~Impl();allocator_->deallocate(impl_);}}
DtlsAssociation::DtlsAssociation(DtlsAssociation&&s) noexcept:impl_(std::exchange(s.impl_,nullptr)),allocator_(s.allocator_){}
DtlsAssociation& DtlsAssociation::operator=(DtlsAssociation&&s) noexcept {if(this!=&s){if(impl_){impl_->~Impl();allocator_->deallocate(impl_);}impl_=std::exchange(s.impl_,nullptr);allocator_=s.allocator_;}return *this;}
Result<DtlsAssociation> DtlsAssociation::create(Allocator&a,Clock&c,DatagramIO&io,AuthProvider&auth,DtlsConfig cfg,std::span<const std::byte>address) noexcept {
 if(address.empty()||address.size()>128||(cfg.udp_payload_ceiling<maximum_frame_bytes+37||cfg.udp_payload_ceiling>1200)||cfg.handshake_timeout_ms==0)return fail(Error::InvalidArgument);
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
 if(cfg.server&&mbedtls_ssl_set_client_transport_id(&s.ssl,reinterpret_cast<const unsigned char*>(address.data()),address.size())!=0)return fail(Error::AuthenticationFailed);
 return result;
}
TransportCapabilities DtlsAssociation::capabilities() const noexcept {return {true,true,true,false,maximum_frame_bytes,37};}
bool DtlsAssociation::ready() const noexcept {return impl_&&impl_->connected&&!impl_->failed;}
Status DtlsAssociation::advance() noexcept {
 if(!impl_||impl_->failed)return fail(Error::NotReady);auto&s=*impl_;if(s.connected)return s.flush();
 auto now=s.clock->now_ms();if(now<s.started||now-s.started>=s.settings.handshake_timeout_ms){s.failed=true;return fail(Error::Timeout);}
 int r=mbedtls_ssl_handshake(&s.ssl);if(s.failed)return fail(Error::Timeout);if(r==0){s.connected=true;return {};}
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
 int n=mbedtls_ssl_read(&s.ssl,reinterpret_cast<unsigned char*>(packet.data()),packet.size());
 if(n==MBEDTLS_ERR_SSL_WANT_READ||n==MBEDTLS_ERR_SSL_WANT_WRITE)return fail(Error::Busy);
 if(n<=0){s.failed=true;return fail(Error::ChannelFailed);}
 if(static_cast<std::size_t>(n)>capabilities().maximum_frame||static_cast<std::size_t>(n)>b.size())return fail(Error::CapacityExceeded);
 std::memcpy(b.data(),packet.data(),static_cast<std::size_t>(n));return static_cast<std::size_t>(n);
}
}
