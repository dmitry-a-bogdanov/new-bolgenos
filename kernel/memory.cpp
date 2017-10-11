#include <bolgenos-ng/memory.hpp>


#include <bolgenos-ng/error.h>

#include <bolgenos-ng/asm.hpp>
#include <bolgenos-ng/kernel_object.hpp>
#include <bolgenos-ng/log.hpp>
#include <bolgenos-ng/mem_utils.hpp>
#include <bolgenos-ng/memory_region.hpp>
#include <bolgenos-ng/multiboot_info.hpp>
#include <bolgenos-ng/page.hpp>

#include "buddy_allocator.hpp"
#include "page_allocator.hpp"
#include "mallocator.hpp"


void *operator new(size_t, void *address)
{
	return address;
}


namespace {


/// \brief Start of high memory.
///
/// The address of the beginning of high memory.
memory::page_frame_t * const highmem_start
	= reinterpret_cast<memory::page_frame_t *>(0x100000);


void detect_memory_regions();
void initilize_highmem_allocators();

bool is_initialized_flag { false };

} // namespace



namespace {

using memory::MemoryRegion;

using memory::allocators::BuddyAllocator;
using memory::allocators::PageAllocator;
using memory::allocators::Mallocator;


/// \brief High memory.
///
/// High memory region descriptor
MemoryRegion highmem;


/// Buddy system that is built on the \ref highmem memory region.
BuddyAllocator highmem_buddy_allocator;


/// Page allocator that is built on the \ref highmem_buddy_allocator.
PageAllocator highmem_page_allocator;


Mallocator highmem_mallocator;

} // namespace


void *memory::alloc_pages(size_t n) {
	return highmem_page_allocator.allocate(n);
}


void memory::free_pages(void *address) {
	highmem_page_allocator.deallocate(address);
}


void memory::init() {
	detect_memory_regions();
	initilize_highmem_allocators();
	is_initialized_flag = true;
}


bool memory::is_initialized()
{
    return is_initialized_flag;
}


void *memory::kmalloc(size_t bytes) {
    if (!memory::is_initialized())
    {
        panic("Memory was not initialized yet!");
    }
	return highmem_mallocator.allocate(bytes);
}

void memory::kfree(void *memory) {
	return highmem_mallocator.deallocate(memory);
}


namespace {


void detect_memory_regions() {
	if (multiboot::boot_info->is_meminfo_valid()) {
		LOG_NOTICE("Detected memory: low = "
			<< multiboot::boot_info->low_memory() << " kB, "
			<< "high = "
			<< multiboot::boot_info->high_memory() << " kB");
	} else {
		panic("Bootloader didn't provide memory info!\n");
	}

	auto highmem_bytes = multiboot::boot_info->high_memory() * 1024;

	highmem.begin(highmem_start);
	highmem.end(highmem_start + memory::align_down<memory::PAGE_SIZE>(highmem_bytes) / memory::PAGE_SIZE);
}


void initilize_highmem_allocators() {
	auto *last_kernel_page = reinterpret_cast<memory::page_frame_t *>(
			memory::align_up<memory::PAGE_SIZE>(kobj::end()));

	highmem_buddy_allocator.initialize(&highmem);
	highmem_page_allocator.initialize(&highmem_buddy_allocator,
			last_kernel_page);
	constexpr size_t chain_memory = 1024*1024; // 1 MB
	constexpr size_t chains = 33; // 32th step is 512
	highmem_mallocator.initialize(&highmem_page_allocator, chain_memory,
			chains);
}

} // namespace

