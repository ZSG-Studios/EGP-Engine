// Original experimental RTC Processor profile. MIT licensed.
#include "processor_profile.hpp"
namespace rtc::impl {
Processor::Processor(std::size_t,bool control,retirement::Token group):pool_(active_processor_pool()),lane_(control?pa::TaskClass::Control:pa::TaskClass::Data){
    if(!pool_)throw ProcessorFailure();
    auto registered=pool_->try_register(control?pa::AssociationRole::Control:pa::AssociationRole::State,{8,8,16384,4096},group);
    if(registered.status!=pa::Admission::Accepted)throw ProcessorFailure(registered.status);owner_=registered.owner;
}
Processor::~Processor(){join();}
void Processor::join(){
    auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(pool_->try_retire(owner_)==pa::Admission::Contended){if(std::chrono::steady_clock::now()>=end)std::terminate();std::this_thread::yield();}
    if(pool_->wait_tasks_retired(owner_,end)!=pa::Wait::Complete)std::terminate();
}
}
