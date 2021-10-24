#include "VirtualMemory.h"
#include "PhysicalMemory.h"

void clearTable(uint64_t frameIndex) {
    for (uint64_t i = 0; i < PAGE_SIZE; ++i) {
        PMwrite(frameIndex * PAGE_SIZE + i, 0);
    }
}

void Transcribe(uint64_t virtualAddress, word_t *arr, word_t *offset) {
    uint64_t m = PAGE_SIZE - 1;
    *offset = virtualAddress & m;
    virtualAddress = virtualAddress >> OFFSET_WIDTH;
    for (int i = 0; i < TABLES_DEPTH; ++i) {
        arr[i] = virtualAddress & m;
        virtualAddress >>= OFFSET_WIDTH;
    }
}

void VMinitialize() {
    clearTable(0);
}


int FindMaximalFrame(uint64_t FinalAddress, uint64_t * Address, int depth, int weight, int * frameNumber, uint64_t path, uint64_t* father) {
    int max = 0;
    word_t frame_index = 0 ;
    int weight_add_on = 0;
    int weight_cpy = weight;
    if (depth < TABLES_DEPTH) {
        for (int index = 0; index < PAGE_SIZE; index++) {
            PMread(FinalAddress * PAGE_SIZE + index, &frame_index);
            if (frame_index != 0) {
                weight_add_on += index % 2 == 0? WEIGHT_EVEN: WEIGHT_ODD;
                weight_cpy =
                        FindMaximalFrame(frame_index, Address,depth + 1,weight + weight_add_on, frameNumber, ((path << OFFSET_WIDTH) + index), father);
               if ((depth + 1) == TABLES_DEPTH){ weight_cpy +=  (path <<  OFFSET_WIDTH) % 2  == 0? WEIGHT_EVEN: WEIGHT_ODD;}
                if (weight_cpy > max) {
                    max = weight_cpy;
                    if ((depth + 1) == TABLES_DEPTH){
                        *frameNumber = frame_index;
                        *father = FinalAddress * PAGE_SIZE + index;
                        (*Address) = (path << OFFSET_WIDTH) + index;
                    }
                }
            }
        }
    }
    return weight_cpy;
}

bool TraverseTree(uint64_t Caller, uint64_t * MaxFrame, uint64_t* EmptyFrame, word_t CurrFrame, int Depth, int Offset, uint64_t ancestor, bool isEmpty){
    if (CurrFrame + 1 > NUM_FRAMES){
        return false;
    }
    if (((uint64_t) (CurrFrame)) > (*MaxFrame)){
        *MaxFrame = CurrFrame;
    }
    if (Depth == TABLES_DEPTH){
        return false;
    }
    isEmpty = true;
    for (int index = 0 ; index < PAGE_SIZE ; index++){
        word_t NewFrame = 0;
        PMread(CurrFrame * PAGE_SIZE + index, &NewFrame);
        if (NewFrame != 0){
            isEmpty = false;
            if (TraverseTree(Caller, MaxFrame, EmptyFrame, NewFrame, Depth + 1, index, CurrFrame, isEmpty)){
                return true;
            }
        }

    }
    if ((isEmpty) && (((uint64_t)(CurrFrame)) != Caller) ){
        *EmptyFrame = CurrFrame ;
        PMwrite(ancestor * PAGE_SIZE + Offset ,0);
        return true;
    }
    return false;
}



uint64_t FindFrame(uint64_t CurrAddress){
    uint64_t MaxFrame = 0;
    uint64_t EmptyFrame = 0;
    uint64_t Address = 0;
    int frameNumber = 0;
    word_t CurrFrame = 0;
    uint64_t ancestor = 0;
    if ((TraverseTree(CurrAddress, &MaxFrame, &EmptyFrame, CurrFrame ,0, 0, 0,false)) && (EmptyFrame != 0)){
        return EmptyFrame;
    }

    if (MaxFrame + 1 < NUM_FRAMES){
        clearTable(MaxFrame + 1);
        return MaxFrame + 1;
    }

    FindMaximalFrame(0 ,&Address , 0, 0, &frameNumber, 0, &ancestor);
    PMevict(frameNumber , Address);
    clearTable(frameNumber);
    PMwrite(ancestor,0);
    return frameNumber;
}




uint64_t GetPage(uint64_t VirtualAddress) {
    word_t Offset = 0;
    word_t arr[TABLES_DEPTH] ;
    Transcribe(VirtualAddress, arr, &Offset);
    uint64_t currentFrame = 0;
    word_t frame = 0;
    word_t frame_cpy = 0;
    for (int i = TABLES_DEPTH - 1 ; i >= 0 ; i--) {
        currentFrame = (currentFrame * PAGE_SIZE) + arr[i];
        PMread(currentFrame, &frame);
        if (frame == 0) {
            frame = FindFrame(frame_cpy);
            PMwrite(currentFrame, frame);
        }
        frame_cpy = frame;
        currentFrame = frame;
    }
    PMrestore(currentFrame, (VirtualAddress >> OFFSET_WIDTH));
    return currentFrame * PAGE_SIZE + Offset;
}

/*** reads a word from the given virtual address
 * and puts its content in *value.
 *
 * returns 1 on success.
 * returns 0 on failure (if the address cannot be mapped to a physical
 * address for any reason)
 ***/
int VMread(uint64_t virtualAddress, word_t* value) {
    if ((virtualAddress >= VIRTUAL_MEMORY_SIZE)){
        return 0;
    }
    uint64_t PageNumber = GetPage(virtualAddress);
    PMread(PageNumber, value);
    return 1;
}

/*** writes a word to the given virtual address
 *
 * returns 1 on success.
 * returns 0 on failure (if the address cannot be mapped to a physical
 * address for any reason)
 ***/
int VMwrite(uint64_t virtualAddress, word_t value) {
    if ((virtualAddress >= VIRTUAL_MEMORY_SIZE)){
        return 0;
    }
    uint64_t PageNumber = GetPage(virtualAddress);
    PMwrite(PageNumber, value);
    return 1;
}
