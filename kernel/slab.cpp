#include <bolgenos-ng/slab.hpp>

#include <bolgenos-ng/error.h>
#include <bolgenos-ng/memory.h>
#include <bolgenos-ng/mem_utils.h>
#include <bolgenos-ng/printk.h>

#include "config.h"



slab_area::slab_area(size_t elem_size, size_t nelems) {
	_elem_size = elem_size;
	_nelems = nelems;
	size_t required_memory = _elem_size * _nelems + _nelems;
	size_t required_pages =
		align_up(required_memory, PAGE_SIZE) / PAGE_SIZE;
	void *area = alloc_pages(required_pages);
	if (!area) {
		_initialized = false;
		return;
	}
	_allocation_map = reinterpret_cast<uint8_t *>(area);
	_memory = ((uint8_t *) _allocation_map) + nelems;
	memset(_allocation_map, MEM_FREE, _nelems);
	/*
	for (size_t chunk = 0; chunk != slab->nelems; ++chunk) {
		printk("Checking chunk %lu:%lu\n", (long unsigned) chunk,
			(long unsigned) slab->allocation_map[chunk]);
	}
	*/
	_initialized = true;
}

slab_area::~slab_area() {
	free_pages(_memory);
	_memory = nullptr;
}

void *slab_area::allocate() {
	void *free_mem = nullptr;
	for (size_t chunk = 0; chunk != this->_nelems; ++chunk) {
		if (get_status(chunk) == MEM_FREE) {
			size_t offset = chunk * this->_elem_size;
			free_mem = (void *) (((size_t) (this->_memory)) +
				offset);
			set_status(chunk, MEM_USED);
			return free_mem;
		}
	}
	return free_mem;
}


void slab_area::deallocate(void *addr) {
	if (!addr) {
		return;
	}
	size_t chunk = (((size_t) addr) - ((size_t) this->_memory)) /
		this->_elem_size;
	set_status(chunk, MEM_FREE);
}

int slab_area::get_status(size_t index) {
	return this->_allocation_map[index];
}


void slab_area::set_status(size_t index, int status) {
	this->_allocation_map[index] = status;
}

