#include "m61.hh"
#include <cstdlib>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cinttypes>
#include <cassert>
#include <sys/mman.h>
#include <map>
#include <iostream>

struct m61_memory_buffer {
    char* buffer;
    size_t pos = 0;
    size_t size = 8 << 20; /* 8 MiB */
    //size_t takenSize = 0;
    std::map<void*, bool>   active_sizes;
    std::map<void*, size_t> size_allocation;
    m61_statistics stats;
    size_t maxPagesCoalesce;
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
    this->maxPagesCoalesce  = 850;
}

m61_memory_buffer::~m61_memory_buffer() {
    munmap(this->buffer, this->size);
}

// ======================================================
// m61_malloc(size_t sz, const char* file, int line) ====
// ======================================================

void* thePtr;
void* m61_malloc(size_t sz, const char* file, int line) {


    (void) file, (void) line;   


    if (!checkIfPossibleToAllocate(sz)){
    	return nullptr;
    }

    thePtr = m61_find_free_space(sz);
    if (thePtr == nullptr){
    	int valuefor16;
	valuefor16 = 16 - ((default_buffer.pos + sz) % 16);
	//printf("value 16 => %ld \n" , valuefor16);
    	allocateInMaps(sz);
    	//thePtr = calculatePositionFor16AligementsBytes(sz);
	thePtr = &default_buffer.buffer[default_buffer.pos];
        default_buffer.pos += (sz + valuefor16);
	++default_buffer.stats.nactive;
    	default_buffer.stats.active_size += sz ;
    	++default_buffer.stats.ntotal;
    	default_buffer.stats.total_size += sz;
    }
    return thePtr;
}

// ======================================================
// =========> m61_find_free_space(size_t sz)   ==========
// ======================================================

void* m61_find_free_space(size_t sz) {

bool theLastWasTrue = false;
size_t freedSpace   = 0;
size_t lastAttempt  = 0;
void* key;
std::map<void*, bool>::iterator   iteratorOnActive;
std::map<void*, size_t>::iterator element;
std::map<void*, size_t> freed_allocation_set;

    //printf("m61_find_free_space : sz -> %ld \n", sz);
    mainLoop:
    for (element = default_buffer.size_allocation.begin() ; element != default_buffer.size_allocation.end(); element++) {

	    //printf("m61_find_free_space : -> default.loop element->first %p, element->second %d \n", element->first, default_buffer.active_sizes.at(element->first));
	    
	    if( default_buffer.active_sizes.at(element->first) == true){
	    
	    	if (!theLastWasTrue) {
	    	    key = element->first;
	    	}
	    	theLastWasTrue = true;
	    	
	    	if(element->second >= sz){
	    	        createStatistics(element, sz);
	    	        //printf("m61_find_free_space : -> element->second >= sz \n", sz);
	    		return element->first;
	    	}
	    	
		for (iteratorOnActive = default_buffer.active_sizes.find(element->first); iteratorOnActive != default_buffer.active_sizes.end(); iteratorOnActive++){
		
	             if( default_buffer.active_sizes.at(iteratorOnActive->first) == true){
	             	freedSpace += default_buffer.size_allocation.at(iteratorOnActive->first);
	                //printf("m61_find_free_space : found freedSpace %ld \n" , freedSpace);
	             }else{
	                //printf("m61_find_free_space : -> iteratorOnActive-> false -freedSpace %ld \n" , freedSpace);
	                freedSpace  = 0;
	                theLastWasTrue = false;
	                break;
	             }
	             
	             if(freedSpace >= sz) {
	              	//printf("m61_find_free_space : -> found a spot freedSpace -> %ld for sz -> %ld at -> %p !\n" , freedSpace, sz, element->first,freedSpace);
	                modyfingTheMaps(element->first,freedSpace);
	                changingStatistics(freedSpace, sz); 
			return element->first;
	             }
		 
		}
               freedSpace  = 0;
	  }else{
	   //printf("m61_find_free_space -> myGUESS is here \n");
	   theLastWasTrue = false;
	  }
       }
       
      if (theLastWasTrue){
           //printf("m61_find_free_space : -> coaleseWithValueBefore ! \n");
           coaleseWithValueBefore(key);
       }
       
       return nullptr;

}

// ======================================================
// ===> m61_free(void* ptr, const char* file, int line)==
// ======================================================

void m61_free(void* ptr, const char* file, int line) {
(void) ptr, (void) file, (void) line;
bool coaleseBefore = false;
bool coaleseAfter  = false;
size_t sizeBefore  = 0;
size_t sizeAfter   = 0;
size_t sizeCurrent = 0;
int    sizeWanted  = 0;
std::map<void*, bool>::iterator prevIterator;
std::map<void*, bool>::iterator nextIterator;
std::map<void*, bool>::iterator iterator; 


    if (ptr == nullptr){
    	return;
    }

   //printf("Liberated pointer : -> %p , of size %ld \n", ptr, default_buffer.size_allocation.at(ptr));     
    iterator = default_buffer.active_sizes.find(ptr);
    if (iterator != default_buffer.active_sizes.end()){
    
        prevIterator = std::prev(iterator, 1);
        nextIterator = std::next(iterator, 1);
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

        
        if(coaleseBefore && coaleseAfter && !(sizeBefore >= default_buffer.maxPagesCoalesce) && !(sizeAfter >= default_buffer.maxPagesCoalesce)){
        //printf("CoaleseBefore & CoaleseAfter : -> %p \n", ptr);     
        eraseInBothMaps(iterator->first);
        eraseInBothMaps(nextIterator->first);

        default_buffer.size_allocation.at(prevIterator->first) = (sizeAfter + sizeCurrent + sizeBefore);
        default_buffer.active_sizes.at(prevIterator->first) = true;
        
        --default_buffer.stats.nactive;
        default_buffer.stats.active_size -= sizeCurrent;

        return;
        
        }else if (coaleseBefore && !coaleseAfter && !(sizeBefore >= default_buffer.maxPagesCoalesce)){
        //printf("CoaleseBefore  : -> %p \n", ptr);     
        eraseInBothMaps(iterator->first);
        
        default_buffer.size_allocation.at(prevIterator->first) = (sizeCurrent + sizeBefore);
        default_buffer.active_sizes.at(prevIterator->first) = true;
        
        --default_buffer.stats.nactive;
        default_buffer.stats.active_size -= sizeCurrent;
        
        return;
        
        }else if (!coaleseBefore && coaleseAfter && !(sizeAfter >= default_buffer.maxPagesCoalesce)){
        //printf("coaleseAfter  : -> %p \n", ptr);     
	eraseInBothMaps(nextIterator->first);
	
	default_buffer.size_allocation.at(iterator->first) = (sizeAfter + sizeCurrent); 
	default_buffer.active_sizes.at(iterator->first) = true;
	
        --default_buffer.stats.nactive;
        default_buffer.stats.active_size -= sizeCurrent;
        return;
        
        }
        
        if (sizeCurrent >= 5000) {
        
        slicingTheCurrentMemory(iterator->first, sizeCurrent, 992);
        
        }
        //printf("No option found in free  : -> %p \n", ptr);     
        default_buffer.active_sizes.at(iterator->first) = true;
        --default_buffer.stats.nactive;
        default_buffer.stats.active_size -= sizeCurrent;
        return;
    }else{
      return;
    }
	

}


// ======================================================
// ============> m61_calloc(count, sz, file, line) ======
// ======================================================
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


// ======================================================
// ============> m61_print_statistics()            ======
// ======================================================


void m61_print_statistics() {
    m61_statistics stats = m61_get_statistics();
    printf("alloc count: active %10llu   total %10llu   fail %10llu\n",
           stats.nactive, stats.ntotal, stats.nfail);
    printf("alloc size:  active %10llu   total %10llu   fail %10llu\n",
           stats.active_size, stats.total_size, stats.fail_size);
}


// ======================================================
// ============> m61_print_leak_report()           ======
// ======================================================

///    Prints a report of all currently-active allocated blocks of dynamic
///    memory.

void m61_print_leak_report() {
    // Your code here.
}


// ======================================================
// ============>    ADDED FUNCTIONALITIES          ======
// ======================================================

void allocateInMaps(size_t sz) {

    default_buffer.size_allocation.insert(std::pair<void*,size_t>(&default_buffer.buffer[default_buffer.pos], sz));
    //printf("allocateInMaps -> %ld , pos => %ld max => %ld \n", sz , default_buffer.pos , default_buffer.size);
    //default_buffer.takenSize += sz;
    default_buffer.active_sizes.insert(std::pair<void*,size_t>(&default_buffer.buffer[default_buffer.pos],false));

}



void* calculatePositionFor16AligementsBytes(size_t sz){
    /*int valuefor16;
    valuefor16 = 16 - ((default_buffer.pos + sz) % 16);
    void* ptrtwo = &default_buffer.buffer[default_buffer.pos];
    default_buffer.pos += (sz + valuefor16);
    return ptrtwo;*/
}

size_t number = 0;
bool checkIfPossibleToAllocate(size_t sz){
    ++number;


    size_t spaceLeftInBuffer = default_buffer.size - default_buffer.pos;
    
    if (sz == 0){
    
    	return false;
    	
    }
    if( (spaceLeftInBuffer +  (default_buffer.pos - default_buffer.stats.active_size)) >= sz){
        return true;
    }
    
    if ((default_buffer.size - default_buffer.pos < sz)){  
    	if (((default_buffer.pos - default_buffer.stats.active_size) <  sz)){
    	
    	   ++default_buffer.stats.nfail;
           default_buffer.stats.fail_size = sz; 
    	   return false;
    	}
    
    }

    return true;

}

void coaleseWithValueBefore(void* key){
          std::map<void*, size_t> copyMap = default_buffer.size_allocation;
          std::map<void*, size_t>::iterator element;
    
       	  for (element = default_buffer.size_allocation.find(key) ; element != default_buffer.size_allocation.end(); element++){
       	  	default_buffer.active_sizes.erase(element->first);
       	  	copyMap.erase(element->first);
       	  	default_buffer.pos -= element->second;
       	  }
       	  default_buffer.size_allocation = copyMap;
       	  
}

void modyfingTheMaps(void* key ,size_t sizeFreed){

        default_buffer.size_allocation.at(key) = sizeFreed;
        default_buffer.active_sizes.at(key) = false;
        std::map<void*, bool>::iterator iterator = default_buffer.active_sizes.find(key);
        std::map<void*, bool> copyMap = default_buffer.active_sizes;
        ++iterator;
	for (; iterator != default_buffer.active_sizes.end(); iterator++){
             if (iterator->second == true){
		 //default_buffer.stats.active_size -= default_buffer.size_allocation.at(iterator->first);
		 default_buffer.size_allocation.erase(iterator->first);
		 copyMap.erase(iterator->first);         
             }else{
             	break;
             }
        } 
        default_buffer.active_sizes = copyMap;
}

void changingStatistics(size_t newSize, size_t sz){
	++default_buffer.stats.nactive;
	default_buffer.stats.active_size += newSize;
	++default_buffer.stats.ntotal;
	default_buffer.stats.total_size += sz;
}

void createStatistics(std::map<void*, size_t>::iterator element, size_t sz){
	default_buffer.active_sizes.at(element->first) =  false;
	++default_buffer.stats.nactive;
	default_buffer.stats.active_size += element->second;
	++default_buffer.stats.ntotal;
	default_buffer.stats.total_size += sz;
}

void eraseInBothMaps(void* key){

	default_buffer.size_allocation.erase(key);
	default_buffer.active_sizes.erase(key);
	
}

void slicingTheCurrentMemory(void* key, size_t sizeCurrent, int sliceWanted){


        size_t sizeWanted = sizeCurrent / sliceWanted;
        
        default_buffer.size_allocation.at(key) = sliceWanted;
        default_buffer.active_sizes.at(key) = true;
	for (int i = 1 ; i != sizeWanted ; i++) {
		
		default_buffer.size_allocation.insert(std::pair<void*,size_t>(key + (i * sliceWanted), sliceWanted)); 
		default_buffer.active_sizes.insert(std::pair<void*,size_t>(key + (i * sliceWanted), true));
		//printf("CreatedSlice : -> %p of value %d \n", key + (i * sliceWanted) , sliceWanted);
	}
	
	if(sizeCurrent - (sizeWanted * sliceWanted) > 0 ){      
		default_buffer.size_allocation.insert(std::pair<void*,size_t>(key + (sizeWanted * sliceWanted), (sizeCurrent - (sizeWanted * sliceWanted)))); 
        	default_buffer.active_sizes.insert(std::pair<void*,size_t>(key + (sizeWanted * sliceWanted), true));
        	//printf("LastSlice :  sizeWanted -> %ld value -> %ld \n",sizeWanted,  sizeCurrent - (sizeWanted * sliceWanted));
        }
}

