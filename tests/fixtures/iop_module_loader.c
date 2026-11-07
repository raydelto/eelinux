/* Exercise the actual loader decision with independent RPC/cache mocks. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
typedef uint32_t u32;
typedef int32_t s32;
#define IOPMOD_MAX_PATH 252
#define IOPMOD_MAX_ARG 252
#define BUILD_BUG_ON(condition) _Static_assert(!(condition), "wire size")
#define pr_err(...) ((void)0)
struct elf32_hdr { bool exports; };
struct irx_export_library { int unused; };
static const struct irx_export_library exporter;
static const struct irx_export_library *irx_first_export_library(const struct elf32_hdr *header)
{ return header->exports ? &exporter : NULL; }
static const struct irx_export_library *irx_next_export_library(
 const struct irx_export_library *entry, const struct elf32_hdr *header)
{ (void)entry; (void)header; return NULL; }
#define irx_for_each_export_library(cursor, header) \
 for ((cursor)=irx_first_export_library(header); (cursor); \
      (cursor)=irx_next_export_library((cursor), (header)))
static unsigned char copied[1024];
static unsigned allocations, releases, cache_flushes, rpc_calls, registrations;
static int rpc_error, load_status=26, registration_error;
static u32 start_result;
static size_t reply_bytes=8;
static bool allocation_failure, cache_provided;
static int load_file_rpc_client;
enum { rpo_mod_buf_load=6 };
static u32 iop_alloc(size_t count)
{ assert(count<=sizeof(copied)); allocations++; return allocation_failure ? 0 : 0x45000; }
static void *iop_bus_to_virt(u32 address)
{ assert(address==0x45000); return copied; }
static void dma_cache_wback(unsigned long address, size_t count)
{ assert(address==(unsigned long)copied && count<=sizeof(copied)); cache_flushes++; }
static void iop_free(u32 address)
{ assert(address==0x45000); releases++; }
static const char *iop_error_message(int status)
{ (void)status; return "mock"; }
static int errno_for_iop_error(int status)
{ assert(status<0); return -ENOEXEC; }
static int register_libraries(const void *buffer)
{
 registrations++;
 if (registration_error) return registration_error;
 if (((const struct elf32_hdr*)buffer)->exports) cache_provided=true;
 return 0;
}
static int sif_rpc(void *client, int operation, const void *request,
 size_t request_size, void *response, size_t response_size)
{
 const unsigned char *packet=request;
 const struct { s32 status; u32 modres; } reply={load_status,start_result};
 assert(client==&load_file_rpc_client && operation==6);
 assert(request_size==512 && response_size==sizeof(reply));
 assert(*(const u32*)packet==0x45000);
 assert(*(const u32*)(packet+4)==strlen((const char*)packet+260)+1);
 rpc_calls++;
 if (!rpc_error) memcpy(response,&reply,reply_bytes);
 return rpc_error;
}
/* ACTUAL_LOADER_FUNCTIONS */
static int run(bool exports, const char *argument)
{
 const struct elf32_hdr header={exports};
 return iop_module_link_buffer(&header,sizeof(header),argument);
}
int main(int argc, char **argv)
{
 assert(argc==2);
 if (!strcmp(argv[1],"resident")) {
  assert(run(true,"test")==0 && cache_provided);
  assert(registrations==1 && releases==1 && rpc_calls==1 && cache_flushes==1);
  assert(((struct elf32_hdr*)copied)->exports);
 } else if (!strcmp(argv[1],"removable")) {
  start_result=2; assert(run(true,NULL)==0 && cache_provided);
  assert(registrations==1 && releases==1);
 } else if (!strcmp(argv[1],"exit_exporter")) {
  start_result=1; assert(run(true,NULL)==-ENOEXEC);
  assert(!cache_provided && registrations==0 && releases==1);
  start_result=0; assert(run(true,NULL)==0 && cache_provided);
  assert(registrations==1 && releases==2);
 } else if (!strcmp(argv[1],"exit_no_exports")) {
  start_result=1; assert(run(false,NULL)==0);
  assert(!cache_provided && releases==1);
 } else if (!strcmp(argv[1],"unsupported")) {
  const u32 values[]={3,4,0xffffffffu,0xffffff9cu};
  for (unsigned int i=0;i<sizeof(values)/sizeof(values[0]);i++) {
   start_result=values[i];
   assert(run(true,NULL)==-ENOEXEC && run(false,NULL)==-ENOEXEC);
  }
  assert(registrations==0 && !cache_provided && releases==8);
 } else if (!strcmp(argv[1],"incomplete_reply")) {
  reply_bytes=4; assert(run(true,NULL)==-ENOEXEC);
  assert(registrations==0 && !cache_provided && releases==1);
 } else if (!strcmp(argv[1],"errors")) {
  rpc_error=-EIO; assert(run(true,NULL)==-EIO);
  rpc_error=0;load_status=-200;assert(run(true,NULL)==-ENOEXEC);
  assert(registrations==0 && releases==2 && !cache_provided);
  load_status=26; registration_error=-ENOMEM;assert(run(true,NULL)==-ENOMEM);
  assert(registrations==1 && releases==3 && !cache_provided);
  allocation_failure=true;assert(run(true,NULL)==-ENOMEM);
  assert(allocations==4 && releases==3 && rpc_calls==3);
 } else if (!strcmp(argv[1],"argument_overflow")) {
  char argument[253];memset(argument,'a',sizeof(argument)-1);argument[252]=0;
  assert(run(true,argument)==-EOVERFLOW);
  assert(rpc_calls==0 && registrations==0 && releases==1 && !cache_provided);
 } else assert(false);
 return 0;
}
