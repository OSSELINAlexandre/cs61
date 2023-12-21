#include "m61.hh"
#include <cstdlib>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cinttypes>
#include <cassert>
#include <sys/mman.h>
#include <map>


struct m61_memory_buffer {
    char* buffer;
    size_t pos = 0;
    size_t size = 8 << 20; /* 8 MiB */
    size_t takenSize = 0;
    std::map<void*, bool>   active_sizes;
    std::map<void*, size_t> size_allocation;
    m61_statistics stats;

    m61_memory_buffer();
    ~m61_memory_buffer();
};

static m61_memory_buffer default_buffer;


m61_memory_buffer::m61_memory_buffer() {
    void* buf = mmap(nullptr,    // Place the buffer at a random address
        this->size,              // Buffer should be 8 MiB big
        PROT_WRITE,              // We want to read and write the buffer
        MAP_ANON | MAP_PRIVATE, -1, 0);
                                 // We want memory freshly allocated by the OS
    assert(buf != MAP_FAILED);
    this->buffer = (char*) buf;
    this->stats.heap_min = ((uintptr_t) (buf));
    this->stats.heap_max =((uintptr_t) (buf)) +  size;
    this->stats.nactive     = 0;
    this->stats.active_size = 0;
    this->stats.ntotal      = 0;
    this->stats.total_size  = 0;
    this->stats.nfail       = 0;
    this->stats.fail_size   = 0;
}

m61_memory_buffer::~m61_memory_buffer() {
    munmap(this->buffer, this->size);
}




/// m61_malloc(sz, file, line)
///    Returns a pointer to `sz` bytes of freshly-allocated dynamic memory.
///    The memory is not initialized. If `sz == 0`, then m61_malloc may
///    return either `nullptr` or a pointer to a unique allocation.
///    The allocation request was made at source code location `file`:`line`.
void* thePtr;
void* m61_malloc(size_t sz, const char* file, int line) {
    (void) file, (void) line;   


    if (!checkIfPossibleToAllocate(sz)){
    	return nullptr;
    }

    thePtr = m61_find_free_space(sz);
    if (thePtr == nullptr){
    	allocateInMaps(sz);
    	thePtr = calculatePositionFor16AligementsBytes(sz);
	++default_buffer.stats.nactive;
    	default_buffer.stats.active_size += sz;
    	++default_buffer.stats.ntotal;
    	default_buffer.stats.total_size += sz;

    }
    return thePtr;
}


void* m61_find_free_space(size_t sz) {
std::map<void*, size_t> freed_allocation_set;
    for (auto& element : default_buffer.size_allocation)  {

	    if( default_buffer.active_sizes.at(element.first) == true){
	    	
	    	if(element.second >= sz){
		    	default_buffer.active_sizes.at(element.first) =  false;
			++default_buffer.stats.nactive;
			default_buffer.stats.active_size += element.second;
			++default_buffer.stats.ntotal;
			default_buffer.stats.total_size += sz;
	    		return element.first;
	    	}
	    	
	    }
    }
    
    return nullptr;
}


/// m61_free(ptr, file, line)
///    Frees the memory allocation pointed to by `ptr`. If `ptr == nullptr`,
///    does nothing. Otherwise, `ptr` must point to a currently active
///    allocation returned by `m61_malloc`. The free was called at location
///    `file`:`line`.


void m61_free(void* ptr, const char* file, int line) {

bool coaleseBefore = false;
bool coaleseAfter  = false;
size_t sizeBefore  = 0;
size_t sizeAfter   = 0;
size_t sizeCurrent = 0;
int    sizeWanted  = 0;

    (void) ptr, (void) file, (void) line;
    std::map<void*, bool>::iterator iterator; 
    
    if (ptr == nullptr){
    	return;
    }
    iterator = default_buffer.active_sizes.find(ptr);
    if (iterator != default_buffer.active_sizes.end()){
    
        std::map<void*, bool>::iterator prevIterator = std::prev(iterator, 1);
        std::map<void*, bool>::iterator nextIterator = std::next(iterator, 1);
    	sizeCurrent = default_buffer.size_allocation.at(iterator->first);
    
        if(prevIterator != default_buffer.active_sizes.end()){
       
       	  if (default_buffer.active_sizes.at(prevIterator->first) == true) {
          coaleseBefore = true;
          sizeBefore = default_buffer.size_allocation.at(prevIterator->first);
          }
          
        }
        if(nextIterator != default_buffer.active_sizes.end() ){
        
          if (default_buffer.active_sizes.at(nextIterator->first) == true) {
          coaleseAfter = true;
          sizeAfter    = default_buffer.size_allocation.at(nextIterator->first);
          }
        }   
        
        if(coaleseBefore && coaleseAfter && !(sizeBefore >= 425) && !(sizeAfter >= 425)){
        
        default_buffer.stats.active_size -= default_buffer.size_allocation.at(iterator->first);   
        
        default_buffer.size_allocation.erase(iterator->first);
        default_buffer.size_allocation.erase(nextIterator->first);
        default_buffer.active_sizes.erase(iterator->first);
        default_buffer.active_sizes.erase(nextIterator->first);
        
        default_buffer.size_allocation.at(prevIterator->first) = ( sizeAfter + sizeCurrent + sizeBefore);
        default_buffer.active_sizes.at(prevIterator->first) = true;
        --default_buffer.stats.nactive;
        return;
        
        }else if (coaleseBefore && !coaleseAfter && !(sizeBefore >= 850)){
        
        default_buffer.stats.active_size -= default_buffer.size_allocation.at(iterator->first);   
        
        default_buffer.size_allocation.erase(iterator->first);
        default_buffer.active_sizes.erase(iterator->first);
        
        default_buffer.size_allocation.at(prevIterator->first) = ( sizeCurrent + sizeBefore);
        default_buffer.active_sizes.at(prevIterator->first) = true;
        --default_buffer.stats.nactive;
        return;
        
        }else if (!coaleseBefore && coaleseAfter && !(sizeAfter >= 850)){
        
        default_buffer.stats.active_size -= default_buffer.size_allocation.at(iterator->first);   
        
        default_buffer.size_allocation.erase(nextIterator->first);
        default_buffer.active_sizes.erase(nextIterator->first);
        
        default_buffer.size_allocation.at(iterator->first) = (sizeAfter + sizeCurrent);
        default_buffer.active_sizes.at(iterator->first) = true;
        --default_buffer.stats.nactive;
        return;
        
        }
        
        if (sizeCurrent >= 5000) {
        
        sizeWanted = sizeCurrent / 1000;
		for (int i = 0 ; i != sizeWanted ; i++) {
		
		default_buffer.size_allocation.insert(std::pair<void*,size_t>(iterator->first + (i * 1000), 1000)); 
                default_buffer.active_sizes.insert(std::pair<void*,size_t>(iterator->first + (i * 1000), true));
		}      
	  	default_buffer.size_allocation.insert(std::pair<void*,size_t>(iterator->first + (sizeWanted * 1000), (sizeCurrent - (sizeWanted * 1000)))); 
                default_buffer.active_sizes.insert(std::pair<void*,size_t>(iterator->first + (sizeWanted * 1000), true));
        }
        
        default_buffer.active_sizes.at(iterator->first) = true;
        --default_buffer.stats.nactive;
        default_buffer.stats.active_size -= default_buffer.size_allocation.at(iterator->first);
        return;
    }else{
    
        return;
    }
	

}


/// m61_calloc(count, sz, file, line)
///    Returns a pointer a fresh dynamic memory allocation big enough to
///    hold an array of `count` elements of `sz` bytes each. Returned
///    memory is initialized to zero. The allocation request was at
///    location `file`:`line`. Returns `nullptr` if out of memory; may
///    also return `nullptr` if `count == 0` or `size == 0`.
size_t value = 0;
void* m61_calloc(size_t count, size_t sz, const char* file, int line) {

    (void) file, (void) line;   
    value = default_buffer.size / count;
    
    if ( sz > value){
        ++default_buffer.stats.nfail;
        default_buffer.stats.fail_size = sz; 
       return nullptr;
    }
	
    if (count == 0 || sz == 0) {
       return nullptr;
    }
    void* ptr = m61_malloc(count * sz, file, line);
    if (ptr) {
        memset(ptr, 0, count * sz);
    }
    return ptr;
}


m61_statistics m61_get_statistics() {
    // Your code here.
    // The handout code sets all statistics to enormous numbers.
    return default_buffer.stats;
}


/// m61_print_statistics()
///    Prints the current memory statistics.

void m61_print_statistics() {
    m61_statistics stats = m61_get_statistics();
    printf("alloc count: active %10llu   total %10llu   fail %10llu\n",
           stats.nactive, stats.ntotal, stats.nfail);
    printf("alloc size:  active %10llu   total %10llu   fail %10llu\n",
           stats.active_size, stats.total_size, stats.fail_size);
}


/// m61_print_leak_report()
///    Prints a report of all currently-active allocated blocks of dynamic
///    memory.

void m61_print_leak_report() {
    // Your code here.
}

void allocateInMaps(size_t sz) {
    default_buffer.size_allocation.insert(std::pair<void*,size_t>(&default_buffer.buffer[default_buffer.pos], sz));
    default_buffer.takenSize += sz;
    //False mean it isn't available;
    default_buffer.active_sizes.insert(std::pair<void*,size_t>(&default_buffer.buffer[default_buffer.pos],false));

}



void* calculatePositionFor16AligementsBytes(size_t sz){
    int valuefor16;
    valuefor16 = 16 - ((default_buffer.pos + sz) % 16);
    void* ptrtwo = &default_buffer.buffer[default_buffer.pos];
    default_buffer.pos += (sz + valuefor16);
    return ptrtwo;
}

size_t number = 0;
bool checkIfPossibleToAllocate(size_t sz){
    ++number;

    //printf("In checkIfPossibleToAllocate => %ld\n" , number);
    //spaceLeftInBuffer = default_buffer.size - default_buffer.pos;
    
    //printf("size wanted           => %ld \n", sz);    
    //printf("Space left total      => %ld \n", default_buffer.takenSize - default_buffer.stats.active_size);
    //printf("Space left max Buffer => %ld \n", spaceLeftInBuffer);

    if (sz == 0){
    
    	return false;
    	
    }
    if ((default_buffer.size - default_buffer.pos < sz)){
    
    	if (((default_buffer.takenSize - default_buffer.stats.active_size) <  sz)){
    	   ++default_buffer.stats.nfail;
           default_buffer.stats.fail_size = sz; 
    	   return false;
    	}
    
    }
    
     //printf("! I'm not getting it !\n");
     //printf("=========================\n");
    return true;

}

