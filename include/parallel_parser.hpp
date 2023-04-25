//
// Created by Diaz, Diego on 31.3.2023.
//

#ifndef PARALLEL_PARSING_PARALLEL_PARSER_HPP
#define PARALLEL_PARSING_PARALLEL_PARSER_HPP

#include "text_reader.h"
#include "ts_queue.h"
#include "kmer_hash_table.hpp"
#include "functions_math.hpp"
#include <thread>
#include <zlib.h>
#include <cstring>
#include <bitset>
#include <chrono>

enum bio_format{FASTA=0, FASTQ=1, PLAIN=2};

// ==============================================================================================================
// INITIAL VERSION FOR REFERENCE
// ==============================================================================================================

template<class sym_type,
         bool is_gzipped=false>
struct parse_input_ORIGINAL{

    

    void operator()(std::string& input_file, std::string& output_file, off_t chunk_size, size_t active_chunks, size_t n_threads, off_t k, uint64_t min_slots, uint64_t min_abundance){

        // Create the hash table
        uint64_t ht_size = mathfunctions::next_prime(min_slots);
        uint64_t kmer_len = k;
        //BasicAtomicHashTable* basic_atomic_hash_table = new BasicAtomicHashTable(ht_size, kmer_len);
        //BasicAtomicHashTableLong* basic_atomic_hash_table_long = new BasicAtomicHashTableLong(ht_size, kmer_len);

        using chunk_type = text_chunk<sym_type>;

        ts_queue<size_t> in_queue;// thread-safe queue that manage the chunks that are ready to be used
        ts_queue<size_t> out_queue; // thread-safe queue that stores the chunks that can be reused for new chunks
        std::vector<chunk_type> text_chunks;
        int fd = open(input_file.c_str(), O_RDONLY);

        // this is for later: to manage compressed inputs
        gzFile gfd;
        if constexpr (is_gzipped){//managed at compilation time
            gfd = gzdopen(fd, "r");
        }
        //

        //get the file size
        struct stat st{};
        if(stat(input_file.c_str(), &st) != 0)  return;

        size_t format = PLAIN; //manage to get the input format
        std::mutex mtx; //just for debugging (you can remove it afterwards)

        //lambda function that manages IO operations
        //we feed this function to std::thread
        auto io_worker = [&]() -> void {

            off_t rem_bytes = st.st_size;

#ifdef __linux__
            posix_fadvise(fd, 0, rem_bytes, POSIX_FADV_SEQUENTIAL);//tell the linux kernel we will access the file sequentially so it can use the readahead heuristic more effectively
#endif

            size_t chunk_id=0;
            text_chunks.resize(active_chunks);
            off_t tmp_ck_size;

            while(chunk_id<active_chunks && rem_bytes>=k){

                tmp_ck_size = std::min(chunk_size, rem_bytes);
                text_chunks[chunk_id].bytes = tmp_ck_size;
                text_chunks[chunk_id].buffer = (sym_type *)malloc(tmp_ck_size);
                text_chunks[chunk_id].id = chunk_id;

                if constexpr (is_gzipped){
                    rem_bytes = read_chunk_from_gz_file<chunk_type>(gfd, text_chunks[chunk_id], rem_bytes, k-1);
                }else{
                    rem_bytes = read_chunk_from_file<chunk_type>(fd, text_chunks[chunk_id], rem_bytes, k-1);
                }
                in_queue.push(chunk_id);//as soon as we push, the chunks become visible of the worker threads to consume them
                chunk_id++;
            }

            size_t buff_idx;
            while(rem_bytes>=k){
                out_queue.pop(buff_idx);//it will wait until out_strings contains something
                text_chunks[buff_idx].id = chunk_id++;

                if constexpr (is_gzipped){
                    rem_bytes = read_chunk_from_gz_file<chunk_type>(gfd, text_chunks[buff_idx], rem_bytes, k-1);
                }else{
                    rem_bytes = read_chunk_from_file<chunk_type>(fd, text_chunks[buff_idx], rem_bytes, k-1);
                }
                in_queue.push(buff_idx);
            }

            //wait for the chunks to be fully processed
            while(!in_queue.empty());

            //remove the unused chunks from the out queue
            while(!out_queue.empty()){
                out_queue.pop(buff_idx);
            }

            in_queue.done();
            out_queue.done();

            close(fd);
        };

        //lambda functions that hash the kmers in a text chunk
        //note: it is not necessary for this function to be a lambda. It can be a static function
        // CLEAN ORIGINAL
        auto hash_kmers =[&](chunk_type& chunk, size_t format){

            off_t i =0, last;
            size_t n_strings=0;

            switch (format) {
                case PLAIN://one-string-per-line format

                    assert(chunk.syms_in_buff>=k);

                    while(i<k){
                        //TODO compute the fingerprint
                        i++;
                    }

                    //slide a window over the buffer
                    while(i<chunk.syms_in_buff){
                        if(chunk.buffer[i]=='\n'){//we consumed a string
                            last = i+k; //rightmost position of the kmer in the prefix of the next string
                            for(off_t u=i+1;u<=last;u++){
                                 //TODO compute the fingerprint from scratch for the next string
                            }
                            n_strings++;
                        }else{
                            //TODO update the fingerprint
                        }

                        //TODO hash the kmer
                        i++;
                    }

                    break;
                case FASTA: //fasta formta
                    //TODO
                    std::cout<<"Not implemented yet"<<std::endl;
                    break;
                case FASTQ: //fastq format
                    //TODO
                    std::cout<<"Not implemented yet"<<std::endl;
                    break;
                default:
                    std::cout<<"Error : format not recognized"<<std::endl;
                    break;

            }
        };

        //lambda function that gets chunks from the IN queue and calls the hash_kmers lambda
        //we feed this function to std::thread
        auto string_worker = [&](size_t worker_id){

            size_t buff_id;
            bool res;
            size_t consumed_kmers = 0;

            while(true){
                res = in_queue.pop(buff_id);//the thread will wait until there is something to pop
                assert(text_chunks[buff_id].bytes>0);
                if(!res) break;
                hash_kmers(text_chunks[buff_id], format);
                consumed_kmers+=text_chunks[buff_id].syms_in_buff-k+1;
                out_queue.push(buff_id);//the thread will wait until the stack is free to push
            }

            {//TODO just testing
                std::unique_lock lck(mtx);
                std::cout<<"Thread "<<worker_id<<" consumed "<<consumed_kmers<<" kmers "<<std::endl;
            }
        };

        std::vector<std::thread> threads;
        threads.emplace_back(io_worker);
        for(size_t i=0;i<n_threads;i++){
            threads.emplace_back(string_worker, i);
        }

        for(auto & thread : threads){
            //if (thread.joinable())
            thread.join();
        }

        std::vector<chunk_type>().swap(text_chunks);

        //delete basic_atomic_hash_table;
        //remove the pages of the input file from the page cache
#ifdef __linux__
        posix_fadvise(fd, 0, st.st_size, POSIX_FADV_DONTNEED);
#endif
        close(fd);
    
    }
    
};


// ==============================================================================================================
// ATOMIC FLAG VERSION of BASIC HASH TABLE
// ==============================================================================================================
template<class sym_type,
         bool is_gzipped=false>
struct parse_input_atomic_flag{

    

    void operator()(std::string& input_file,  std::string& output_file, off_t chunk_size, size_t active_chunks, size_t n_threads, off_t k, uint64_t min_slots, uint64_t min_abundance){

        auto start_building = std::chrono::high_resolution_clock::now();
        // Create the hash table
        uint64_t ht_size = mathfunctions::next_prime(min_slots);
        uint64_t kmer_len = k;
        //BasicAtomicHashTable* basic_atomic_hash_table = new BasicAtomicHashTable(ht_size, kmer_len);
        BasicAtomicFlagHashTableLong* basic_atomic_hash_table_long = new BasicAtomicFlagHashTableLong(ht_size, kmer_len);

        using chunk_type = text_chunk<sym_type>;

        ts_queue<size_t> in_queue;// thread-safe queue that manage the chunks that are ready to be used
        ts_queue<size_t> out_queue; // thread-safe queue that stores the chunks that can be reused for new chunks
        std::vector<chunk_type> text_chunks;
        int fd = open(input_file.c_str(), O_RDONLY);

        // this is for later: to manage compressed inputs
        gzFile gfd;
        if constexpr (is_gzipped){//managed at compilation time
            gfd = gzdopen(fd, "r");
        }
        //

        //get the file size
        struct stat st{};
        if(stat(input_file.c_str(), &st) != 0)  return;

        size_t format = PLAIN; //manage to get the input format
        std::mutex mtx; //just for debugging (you can remove it afterwards)

        //lambda function that manages IO operations
        //we feed this function to std::thread
        auto io_worker = [&]() -> void {

            off_t rem_bytes = st.st_size;

#ifdef __linux__
            posix_fadvise(fd, 0, rem_bytes, POSIX_FADV_SEQUENTIAL);//tell the linux kernel we will access the file sequentially so it can use the readahead heuristic more effectively
#endif

            size_t chunk_id=0;
            text_chunks.resize(active_chunks);
            off_t tmp_ck_size;

            while(chunk_id<active_chunks && rem_bytes>=k){

                tmp_ck_size = std::min(chunk_size, rem_bytes);
                text_chunks[chunk_id].bytes = tmp_ck_size;
                text_chunks[chunk_id].buffer = (sym_type *)malloc(tmp_ck_size);
                text_chunks[chunk_id].id = chunk_id;

                if constexpr (is_gzipped){
                    rem_bytes = read_chunk_from_gz_file<chunk_type>(gfd, text_chunks[chunk_id], rem_bytes, k-1);
                }else{
                    rem_bytes = read_chunk_from_file<chunk_type>(fd, text_chunks[chunk_id], rem_bytes, k-1);
                }
                in_queue.push(chunk_id);//as soon as we push, the chunks become visible of the worker threads to consume them
                chunk_id++;
            }

            size_t buff_idx;
            while(rem_bytes>=k){
                out_queue.pop(buff_idx);//it will wait until out_strings contains something
                text_chunks[buff_idx].id = chunk_id++;

                if constexpr (is_gzipped){
                    rem_bytes = read_chunk_from_gz_file<chunk_type>(gfd, text_chunks[buff_idx], rem_bytes, k-1);
                }else{
                    rem_bytes = read_chunk_from_file<chunk_type>(fd, text_chunks[buff_idx], rem_bytes, k-1);
                }
                in_queue.push(buff_idx);
            }

            //wait for the chunks to be fully processed
            while(!in_queue.empty());

            //remove the unused chunks from the out queue
            while(!out_queue.empty()){
                out_queue.pop(buff_idx);
            }

            in_queue.done();
            out_queue.done();

            close(fd);
        };

        //lambda functions that hash the kmers in a text chunk
        //note: it is not necessary for this function to be a lambda. It can be a static function

        // MODIFIED LONG
        auto hash_kmers =[&](chunk_type& chunk, size_t format){

            off_t i =0, last;
            size_t n_strings=0;

            RollingHasherDual* rolling_hasher = new RollingHasherDual(ht_size, kmer_len);

            switch (format) {
                case PLAIN://one-string-per-line format
                {
                    int kmer_bytes = kmer_len / 4;
                    if (kmer_len % 4 != 0)
                        kmer_bytes += 1;
                    // Characters in last block
                    int fbmc = kmer_len % 4;
                    if (fbmc == 0)
                        fbmc = 4;
                    // Store k-mer as string here
                    //std::vector<uint64_t> kmer_string(kmer_blocks,0);
                    uint8_t* kmer_string = new uint8_t[kmer_bytes];
                    uint8_t* kmer_string_reverse = new uint8_t[kmer_bytes];
                    int lshift = kmer_len % 4;
                    int rshift = 4 - lshift;  
                    // Store k-mer hash value here
                    uint64_t kmer_hash = 0;
                    // Store number of characters stored in the k-mer here
                    uint64_t chars_in_kmer = 0;
                    // Mask for relevant characters in the last k-mer block
                    uint8_t first_block_mask = 0;
                    // New character goes here
                    uint8_t new_char = 0;
                    // Character that drops out the window goes here
                    uint64_t drop_out_char = 0;
                    uint64_t current_check_slot = 0;
                    bool kmer_was_found = false;
                    bool forward_is_canonical = true;

                    for (int mi = 0; mi < fbmc; mi++)
                    {
                        first_block_mask = first_block_mask << 2;
                        first_block_mask = first_block_mask | uint8_t(3);
                    }

                    assert(chunk.syms_in_buff>=k);

                    //slide a window over the buffer
                    while(i<chunk.syms_in_buff){
                        new_char =  uint8_t(twobitstringfunctions::char2int(chunk.buffer[i]));
                        if(new_char > uint8_t(3)){//we encountered non-(A,C,G,T) character
                            kmer_hash = 0;
                            //std::fill(kmer_string.begin(), kmer_string.end(), 0);
                            for (int ii = 0; ii < kmer_bytes; ii++)
                                kmer_string[ii] = 0;
                            chars_in_kmer = 0;
                            n_strings++;
                            rolling_hasher->reset();
                        } else {
                            // First get the drop out char
                            //std::cout << "NEW CHAR IS : " << new_char << "\n";
                            //std::cout << "NEW CHAR after shift IS : " << (new_char<<62) << "\n";

                            drop_out_char = uint64_t((kmer_string[0] >> 2*(fbmc-1)) & uint8_t(3));
                            //std::cout << "K-MER STRING IS: " << (kmer_string[0] >> 2*28) << "\n";
                            //std::cout << "DROP PUT CHAR IS: " << drop_out_char << "\n";
                            // Update k-mer string
                            for (int ci = 0; ci < kmer_bytes-1; ci++)
                            {
                                kmer_string[ci] <<= 2;
                                kmer_string[ci] |= (kmer_string[ci+1] >> 6);
                            }
                            kmer_string[0] &= first_block_mask;
                            kmer_string[kmer_bytes-1] <<=2;
                            kmer_string[kmer_bytes-1] |= new_char;

                            // Update hash value
                            rolling_hasher->update_rolling_hash(new_char, drop_out_char);
                            chars_in_kmer = std::min(chars_in_kmer+1, kmer_len);
                                                        
                            // Build reverse k-mer
                            // I) Swap bytes and characters within bytes
                            for (int ci = 0; ci < kmer_bytes; ci++)
                            {
                                kmer_string_reverse[ci] = ((kmer_string[kmer_bytes-1-ci] & uint8_t(240)) >> 4) | ((kmer_string[kmer_bytes-1-ci] & uint8_t(15)) << 4);
                                kmer_string_reverse[ci] = ((kmer_string_reverse[ci] & uint8_t(204)) >> 2) | ((kmer_string_reverse[ci] & uint8_t(51)) << 2);
                            }
                            // II) Shift bytes
                            if (lshift != 0)
                            {
                                for (int ci = kmer_bytes-1; ci > 0; ci--)
                                {
                                    kmer_string_reverse[ci] = ((kmer_string_reverse[ci] >> 2*rshift) | (kmer_string_reverse[ci-1] << 2*lshift));
                                }
                                kmer_string_reverse[0] = kmer_string_reverse[0] >> 2*rshift;    
                            }
                            // III) And complement characters
                            for (int ci = 0; ci < kmer_bytes; ci++)
                            {
                                kmer_string_reverse[ci] = ~kmer_string_reverse[ci];
                            }
                            kmer_string_reverse[0] &= first_block_mask;

                            //std::cout << "Forward : " << std::bitset<8>(kmer_string[0]) << " " <<  std::bitset<8>(kmer_string[1]) << " " << std::bitset<8>(kmer_string[2]) << "\n";
                            //std::cout << "Reverse : " << std::bitset<8>(kmer_string_reverse[0])<< " " << std::bitset<8>(kmer_string_reverse[1])<< " " << std::bitset<8>(kmer_string_reverse[2]) << "\n";
                        
                            // Determine correct orientation
                            kmer_hash = rolling_hasher->get_current_hash_forward();
                            forward_is_canonical = true;
                            
                            for (int icc = 0; icc < kmer_bytes; icc++)
                            {
                                if (kmer_string_reverse[icc] < kmer_string[icc])
                                {
                                    forward_is_canonical = false;
                                    kmer_hash = rolling_hasher->get_current_hash_backward();
                                    break;
                                }
                                else if (kmer_string_reverse[icc] > kmer_string[icc])
                                {
                                    break;
                                }
                            }
                            // Insert the k-mer in the hash table
                            current_check_slot = kmer_hash;
                            kmer_was_found = false;
                            uint64_t probing_round = 0;
                            bool slot_is_in_use = false;
                            bool handled_successfully = false;
                            if (chars_in_kmer >= k)
                            {
                                if (forward_is_canonical)
                                {
                                    while(true)
                                    {
                                        probing_round=1;
                                        // First, acquire the lock
                                        while (basic_atomic_hash_table_long->kmer_locks[current_check_slot].test_and_set(std::memory_order_acquire));
                                        // Then, check the k-mer
                                        // If count == 0, no k-mer -> insert as new
                                        if (basic_atomic_hash_table_long->counts[current_check_slot] == 0)
                                        {
                                            basic_atomic_hash_table_long->counts[current_check_slot] = 1;
                                            std::memcpy(&(basic_atomic_hash_table_long->kmer_array[kmer_bytes*current_check_slot]), kmer_string, kmer_bytes);
                                            basic_atomic_hash_table_long->kmer_locks[current_check_slot].clear(std::memory_order_release);
                                            break;
                                        }
                                        // k-mer exists in the slot, check if it matches
                                        // If match, k-mer was found
                                        else if (std::memcmp(&(basic_atomic_hash_table_long->kmer_array[kmer_bytes*current_check_slot]), kmer_string, kmer_bytes) == 0)
                                        {
                                            // Increase count and done
                                            basic_atomic_hash_table_long->counts[current_check_slot] += 1;
                                            basic_atomic_hash_table_long->kmer_locks[current_check_slot].clear(std::memory_order_release);
                                            break;
                                        }
                                        // Otherwise, probe to next position
                                        else
                                        {
                                            basic_atomic_hash_table_long->kmer_locks[current_check_slot].clear(std::memory_order_release);
                                            current_check_slot = current_check_slot + (probing_round*probing_round);
                                            current_check_slot = current_check_slot % ht_size;
                                            if (current_check_slot == kmer_hash)
                                            {
                                                std::cout << "Hash table is full... Cannot handle this yet\n";
                                                return;
                                            }
                                        }
                                    }
                                }
                                else
                                {
                                    while(true)
                                    {
                                        probing_round=1;
                                        // First, acquire the lock
                                        while (basic_atomic_hash_table_long->kmer_locks[current_check_slot].test_and_set(std::memory_order_acquire));
                                        // Then, check the k-mer
                                        // If count == 0, no k-mer -> insert as new
                                        if (basic_atomic_hash_table_long->counts[current_check_slot] == 0)
                                        {
                                            basic_atomic_hash_table_long->counts[current_check_slot] = 1;
                                            std::memcpy(&(basic_atomic_hash_table_long->kmer_array[kmer_bytes*current_check_slot]), kmer_string_reverse, kmer_bytes);
                                            basic_atomic_hash_table_long->kmer_locks[current_check_slot].clear(std::memory_order_release);
                                            break;
                                        }
                                        // k-mer exists in the slot, check if it matches
                                        // If match, k-mer was found
                                        else if (std::memcmp(&(basic_atomic_hash_table_long->kmer_array[kmer_bytes*current_check_slot]), kmer_string_reverse, kmer_bytes) == 0)
                                        {
                                            // Increase count and done
                                            basic_atomic_hash_table_long->counts[current_check_slot] += 1;
                                            basic_atomic_hash_table_long->kmer_locks[current_check_slot].clear(std::memory_order_release);
                                            break;
                                        }
                                        // Otherwise, probe to next position
                                        else
                                        {
                                            basic_atomic_hash_table_long->kmer_locks[current_check_slot].clear(std::memory_order_release);
                                            current_check_slot = current_check_slot + (probing_round*probing_round);
                                            current_check_slot = current_check_slot % ht_size;
                                            if (current_check_slot == kmer_hash)
                                            {
                                                std::cout << "Hash table is full... Cannot handle this yet\n";
                                                return;
                                            }
                                        }
                                    }
                                }
                                
                            }
                        }
                        i++;
                    }
                    std::cout << "Chunk done\n";
                    break;
                }
                case FASTA: //fasta formta
                    //TODO
                    std::cout<<"Not implemented yet"<<std::endl;
                    break;
                case FASTQ: //fastq format
                    //TODO
                    std::cout<<"Not implemented yet"<<std::endl;
                    break;
                default:
                    std::cout<<"Error : format not recognized"<<std::endl;
                    break;

            }
            delete rolling_hasher;
        };

        //lambda function that gets chunks from the IN queue and calls the hash_kmers lambda
        //we feed this function to std::thread
        auto string_worker = [&](size_t worker_id){

            size_t buff_id;
            bool res;
            size_t consumed_kmers = 0;

            while(true){
                res = in_queue.pop(buff_id);//the thread will wait until there is something to pop
                assert(text_chunks[buff_id].bytes>0);
                if(!res) break;
                hash_kmers(text_chunks[buff_id], format);
                consumed_kmers+=text_chunks[buff_id].syms_in_buff-k+1;
                out_queue.push(buff_id);//the thread will wait until the stack is free to push
            }

            {//TODO just testing
                std::unique_lock lck(mtx);
                std::cout<<"Thread "<<worker_id<<" consumed "<<consumed_kmers<<" kmers "<<std::endl;
            }
        };

        std::vector<std::thread> threads;
        threads.emplace_back(io_worker);
        for(size_t i=0;i<n_threads;i++){
            threads.emplace_back(string_worker, i);
        }

        for(auto & thread : threads){
            //if (thread.joinable())
            thread.join();
        }

        std::vector<chunk_type>().swap(text_chunks);

        
        //for (uint64_t i = 0; i < ht_size; i++)
        //{
        //    std::cout << i << " : " << basic_atomic_hash_table_long->counts[i] << "\n";
        //}
        
        //remove the pages of the input file from the page cache
#ifdef __linux__
        posix_fadvise(fd, 0, st.st_size, POSIX_FADV_DONTNEED);
#endif
        close(fd);

        auto start_writing = std::chrono::high_resolution_clock::now();
        if (min_abundance > 0)
            basic_atomic_hash_table_long->write_kmers(min_abundance, output_file);
        auto end_writing = std::chrono::high_resolution_clock::now();
        delete basic_atomic_hash_table_long;

        auto build_duration = std::chrono::duration_cast<std::chrono::microseconds>(start_writing - start_building);
        auto writing_duration = std::chrono::duration_cast<std::chrono::microseconds>(end_writing - start_writing);
        std::cout << "Time used to build hash table: " << build_duration.count() << " microseconds\n";
        std::cout << "Time used to write k-mers in a file: " << writing_duration.count() << " microseconds\n";
    }
    
};


// ==============================================================================================================
// POINTER HASH TABLE VERSION, ATOMIC FLAGS
// ==============================================================================================================
template<class sym_type,
         bool is_gzipped=false>
struct parse_input_pointer_atomic_flag{

    

    void operator()(std::string& input_file,  std::string& output_file, off_t chunk_size, size_t active_chunks, size_t n_threads, off_t k, uint64_t min_slots, uint64_t min_abundance){

        bool print_times = false;
        auto start_building = std::chrono::high_resolution_clock::now();
        // Create the hash table
        uint64_t ht_size = mathfunctions::next_prime(min_slots);
        uint64_t kmer_len = k;
        //BasicAtomicHashTable* basic_atomic_hash_table = new BasicAtomicHashTable(ht_size, kmer_len);
        uint64_t kmer_blocks = std::ceil(kmer_len/32.0);
        PointerHashTableCanonicalAF* hash_table = new PointerHashTableCanonicalAF(ht_size, kmer_len, kmer_blocks);

        using chunk_type = text_chunk<sym_type>;

        ts_queue<size_t> in_queue;// thread-safe queue that manage the chunks that are ready to be used
        ts_queue<size_t> out_queue; // thread-safe queue that stores the chunks that can be reused for new chunks
        std::vector<chunk_type> text_chunks;
        int fd = open(input_file.c_str(), O_RDONLY);

        // this is for later: to manage compressed inputs
        gzFile gfd;
        if constexpr (is_gzipped){//managed at compilation time
            gfd = gzdopen(fd, "r");
        }
        //

        //get the file size
        struct stat st{};
        if(stat(input_file.c_str(), &st) != 0)  return;

        size_t format = PLAIN; //manage to get the input format
        std::mutex mtx; //just for debugging (you can remove it afterwards)

        //lambda function that manages IO operations
        //we feed this function to std::thread
        auto io_worker = [&]() -> void {

            off_t rem_bytes = st.st_size;

#ifdef __linux__
            posix_fadvise(fd, 0, rem_bytes, POSIX_FADV_SEQUENTIAL);//tell the linux kernel we will access the file sequentially so it can use the readahead heuristic more effectively
#endif

            size_t chunk_id=0;
            text_chunks.resize(active_chunks);
            off_t tmp_ck_size;

            while(chunk_id<active_chunks && rem_bytes>=k){

                tmp_ck_size = std::min(chunk_size, rem_bytes);
                text_chunks[chunk_id].bytes = tmp_ck_size;
                text_chunks[chunk_id].buffer = (sym_type *)malloc(tmp_ck_size);
                text_chunks[chunk_id].id = chunk_id;

                if constexpr (is_gzipped){
                    rem_bytes = read_chunk_from_gz_file<chunk_type>(gfd, text_chunks[chunk_id], rem_bytes, k-1);
                }else{
                    rem_bytes = read_chunk_from_file<chunk_type>(fd, text_chunks[chunk_id], rem_bytes, k-1);
                }
                in_queue.push(chunk_id);//as soon as we push, the chunks become visible of the worker threads to consume them
                chunk_id++;
            }

            size_t buff_idx;
            while(rem_bytes>=k){
                out_queue.pop(buff_idx);//it will wait until out_strings contains something
                text_chunks[buff_idx].id = chunk_id++;

                if constexpr (is_gzipped){
                    rem_bytes = read_chunk_from_gz_file<chunk_type>(gfd, text_chunks[buff_idx], rem_bytes, k-1);
                }else{
                    rem_bytes = read_chunk_from_file<chunk_type>(fd, text_chunks[buff_idx], rem_bytes, k-1);
                }
                in_queue.push(buff_idx);
            }

            //wait for the chunks to be fully processed
            while(!in_queue.empty());

            //remove the unused chunks from the out queue
            while(!out_queue.empty()){
                out_queue.pop(buff_idx);
            }

            in_queue.done();
            out_queue.done();

            close(fd);
        };

        //lambda functions that hash the kmers in a text chunk
        //note: it is not necessary for this function to be a lambda. It can be a static function

        // MODIFIED LONG
        auto hash_kmers =[&](chunk_type& chunk, size_t format){

            off_t i =0, last;
            size_t n_strings=0;

            RollingHasherDual* rolling_hasher = new RollingHasherDual(ht_size, kmer_len);
            // --- Build k-mer factory ---
            KMerFactoryCanonical2BC* kmer_factory = new KMerFactoryCanonical2BC(k);

            switch (format) {
                case PLAIN://one-string-per-line format
                {
                    bool predecessor_kmer_exists = false;
                    uint64_t predecessor_kmer_slot = ht_size;
                    uint64_t new_char = 0;
                    uint64_t current_kmer_slot = ht_size;
                    assert(chunk.syms_in_buff>=k);
                    //slide a window over the buffer
                    while(i<chunk.syms_in_buff){
                        new_char =  uint64_t(twobitstringfunctions::char2int(chunk.buffer[i]));
                        if (new_char > 3ULL)
                            kmer_factory->reset();
                        else
                            kmer_factory->push_new_integer(new_char);
                        if (kmer_factory->get_number_of_stored_characters() == 0)
                        {
                            rolling_hasher->reset();
                            predecessor_kmer_exists = false;
                            predecessor_kmer_slot = ht_size;
                        }
                        else
                        {
                            rolling_hasher->update_rolling_hash(kmer_factory->get_forward_newest_character(), kmer_factory->get_forward_pushed_off_character());
                        }
                        if (kmer_factory->get_number_of_stored_characters() == kmer_len)
                        {
                            current_kmer_slot = hash_table->process_kmer(kmer_factory, rolling_hasher, predecessor_kmer_exists, predecessor_kmer_slot); 
                            predecessor_kmer_exists = true;
                            predecessor_kmer_slot = current_kmer_slot;
                        }
                        i++;
                    }
                    std::cout << "Chunk done\n";
                    break;
                }
                case FASTA: //fasta formta
                    //TODO
                    std::cout<<"Not implemented yet"<<std::endl;
                    break;
                case FASTQ: //fastq format
                    //TODO
                    std::cout<<"Not implemented yet"<<std::endl;
                    break;
                default:
                    std::cout<<"Error : format not recognized"<<std::endl;
                    break;

            }
            delete kmer_factory;
            delete rolling_hasher;
        };

        //lambda function that gets chunks from the IN queue and calls the hash_kmers lambda
        //we feed this function to std::thread
        auto string_worker = [&](size_t worker_id){

            size_t buff_id;
            bool res;
            size_t consumed_kmers = 0;

            while(true){
                res = in_queue.pop(buff_id);//the thread will wait until there is something to pop
                assert(text_chunks[buff_id].bytes>0);
                if(!res) break;
                hash_kmers(text_chunks[buff_id], format);
                consumed_kmers+=text_chunks[buff_id].syms_in_buff-k+1;
                out_queue.push(buff_id);//the thread will wait until the stack is free to push
            }

            {//TODO just testing
                std::unique_lock lck(mtx);
                std::cout<<"Thread "<<worker_id<<" consumed "<<consumed_kmers<<" kmers "<<std::endl;
            }
        };

        std::vector<std::thread> threads;
        threads.emplace_back(io_worker);
        for(size_t i=0;i<n_threads;i++){
            threads.emplace_back(string_worker, i);
        }

        for(auto & thread : threads){
            //if (thread.joinable())
            thread.join();
        }

        std::vector<chunk_type>().swap(text_chunks);
       
        //remove the pages of the input file from the page cache
#ifdef __linux__
        posix_fadvise(fd, 0, st.st_size, POSIX_FADV_DONTNEED);
#endif
        close(fd);

        auto start_writing = std::chrono::high_resolution_clock::now();
        if (min_abundance > 0)
            hash_table->write_kmers_on_disk_separately_even_faster(min_abundance, output_file);
        auto end_writing = std::chrono::high_resolution_clock::now();
        delete hash_table;

        if (print_times)
        {
            auto build_duration = std::chrono::duration_cast<std::chrono::microseconds>(start_writing - start_building);
            auto writing_duration = std::chrono::duration_cast<std::chrono::microseconds>(end_writing - start_writing);
            std::cout << "Time used to build hash table: " << build_duration.count() << " microseconds\n";
            std::cout << "Time used to write k-mers in a file: " << writing_duration.count() << " microseconds\n";
        }
        
    }
    
};

// ==============================================================================================================
// ATOMIC VARIABLES VERSION
// ==============================================================================================================
template<class sym_type,
         bool is_gzipped=false>
struct parse_input_pointer_atomic_variable{

    

    void operator()(std::string& input_file,  std::string& output_file, off_t chunk_size, size_t active_chunks, size_t n_threads, off_t k, uint64_t min_slots, uint64_t min_abundance){

        bool print_times = true;
        bool print_other_stuff = false;
        auto start_building = std::chrono::high_resolution_clock::now();
        // Create the hash table
        uint64_t ht_size = mathfunctions::next_prime(min_slots);
        uint64_t kmer_len = k;
        //BasicAtomicHashTable* basic_atomic_hash_table = new BasicAtomicHashTable(ht_size, kmer_len);
        uint64_t kmer_blocks = std::ceil(kmer_len/32.0);
        PointerHashTableCanonicalAV* hash_table = new PointerHashTableCanonicalAV(ht_size, kmer_len, kmer_blocks);

        using chunk_type = text_chunk<sym_type>;

        ts_queue<size_t> in_queue;// thread-safe queue that manage the chunks that are ready to be used
        ts_queue<size_t> out_queue; // thread-safe queue that stores the chunks that can be reused for new chunks
        std::vector<chunk_type> text_chunks;
        int fd = open(input_file.c_str(), O_RDONLY);

        // this is for later: to manage compressed inputs
        gzFile gfd;
        if constexpr (is_gzipped){//managed at compilation time
            gfd = gzdopen(fd, "r");
        }
        //

        //get the file size
        struct stat st{};
        if(stat(input_file.c_str(), &st) != 0)  return;

        size_t format = PLAIN; //manage to get the input format
        std::mutex mtx; //just for debugging (you can remove it afterwards)

        //lambda function that manages IO operations
        //we feed this function to std::thread
        auto io_worker = [&]() -> void {

            off_t rem_bytes = st.st_size;

#ifdef __linux__
            posix_fadvise(fd, 0, rem_bytes, POSIX_FADV_SEQUENTIAL);//tell the linux kernel we will access the file sequentially so it can use the readahead heuristic more effectively
#endif

            size_t chunk_id=0;
            text_chunks.resize(active_chunks);
            off_t tmp_ck_size;

            while(chunk_id<active_chunks && rem_bytes>=k){

                tmp_ck_size = std::min(chunk_size, rem_bytes);
                text_chunks[chunk_id].bytes = tmp_ck_size;
                text_chunks[chunk_id].buffer = (sym_type *)malloc(tmp_ck_size);
                text_chunks[chunk_id].id = chunk_id;

                if constexpr (is_gzipped){
                    rem_bytes = read_chunk_from_gz_file<chunk_type>(gfd, text_chunks[chunk_id], rem_bytes, k-1);
                }else{
                    rem_bytes = read_chunk_from_file<chunk_type>(fd, text_chunks[chunk_id], rem_bytes, k-1);
                }
                in_queue.push(chunk_id);//as soon as we push, the chunks become visible of the worker threads to consume them
                chunk_id++;
            }

            size_t buff_idx;
            while(rem_bytes>=k){
                out_queue.pop(buff_idx);//it will wait until out_strings contains something
                text_chunks[buff_idx].id = chunk_id++;

                if constexpr (is_gzipped){
                    rem_bytes = read_chunk_from_gz_file<chunk_type>(gfd, text_chunks[buff_idx], rem_bytes, k-1);
                }else{
                    rem_bytes = read_chunk_from_file<chunk_type>(fd, text_chunks[buff_idx], rem_bytes, k-1);
                }
                in_queue.push(buff_idx);
            }

            //wait for the chunks to be fully processed
            while(!in_queue.empty());

            //remove the unused chunks from the out queue
            while(!out_queue.empty()){
                out_queue.pop(buff_idx);
            }

            in_queue.done();
            out_queue.done();

            close(fd);
        };

        //lambda functions that hash the kmers in a text chunk
        //note: it is not necessary for this function to be a lambda. It can be a static function

        // MODIFIED LONG
        auto hash_kmers =[&](chunk_type& chunk, size_t format){

            off_t i =0, last;
            size_t n_strings=0;

            RollingHasherDual* rolling_hasher = new RollingHasherDual(ht_size, kmer_len);
            // --- Build k-mer factory ---
            KMerFactoryCanonical2BC* kmer_factory = new KMerFactoryCanonical2BC(k);

            switch (format) {
                case PLAIN://one-string-per-line format
                {
                    bool predecessor_kmer_exists = false;
                    uint64_t predecessor_kmer_slot = ht_size;
                    uint64_t new_char = 0;
                    uint64_t current_kmer_slot = ht_size;
                    assert(chunk.syms_in_buff>=k);
                    //slide a window over the buffer
                    while(i<chunk.syms_in_buff){
                        new_char =  uint64_t(twobitstringfunctions::char2int(chunk.buffer[i]));
                        if (new_char > 3ULL)
                            kmer_factory->reset();
                        else
                            kmer_factory->push_new_integer(new_char);
                        if (kmer_factory->get_number_of_stored_characters() == 0)
                        {
                            rolling_hasher->reset();
                            predecessor_kmer_exists = false;
                            predecessor_kmer_slot = ht_size;
                        }
                        else
                        {
                            rolling_hasher->update_rolling_hash(kmer_factory->get_forward_newest_character(), kmer_factory->get_forward_pushed_off_character());
                        }
                        if (kmer_factory->get_number_of_stored_characters() == kmer_len)
                        {
                            //current_kmer_slot = hash_table->process_kmer(kmer_factory, rolling_hasher, predecessor_kmer_exists, predecessor_kmer_slot); 
                            current_kmer_slot = hash_table->process_kmer_MT(kmer_factory, rolling_hasher, predecessor_kmer_exists, predecessor_kmer_slot); 
                            predecessor_kmer_exists = true;
                            predecessor_kmer_slot = current_kmer_slot;
                        }
                        i++;
                    }
                    if (print_other_stuff)
                        std::cout << "Chunk done\n";
                    break;
                }
                case FASTA: //fasta formta
                    //TODO
                    std::cout<<"Not implemented yet"<<std::endl;
                    break;
                case FASTQ: //fastq format
                    //TODO
                    std::cout<<"Not implemented yet"<<std::endl;
                    break;
                default:
                    std::cout<<"Error : format not recognized"<<std::endl;
                    break;

            }
            delete kmer_factory;
            delete rolling_hasher;
        };

        //lambda function that gets chunks from the IN queue and calls the hash_kmers lambda
        //we feed this function to std::thread
        auto string_worker = [&](size_t worker_id){

            size_t buff_id;
            bool res;
            size_t consumed_kmers = 0;

            while(true){
                res = in_queue.pop(buff_id);//the thread will wait until there is something to pop
                assert(text_chunks[buff_id].bytes>0);
                if(!res) break;
                hash_kmers(text_chunks[buff_id], format);
                consumed_kmers+=text_chunks[buff_id].syms_in_buff-k+1;
                out_queue.push(buff_id);//the thread will wait until the stack is free to push
            }

            if (print_other_stuff)
            {//TODO just testing
                std::unique_lock lck(mtx);
                std::cout<<"Thread "<<worker_id<<" consumed "<<consumed_kmers<<" kmers "<<std::endl;
            }
        };

        std::vector<std::thread> threads;
        threads.emplace_back(io_worker);
        for(size_t i=0;i<n_threads;i++){
            threads.emplace_back(string_worker, i);
        }

        for(auto & thread : threads){
            //if (thread.joinable())
            thread.join();
        }

        std::vector<chunk_type>().swap(text_chunks);

        
        //remove the pages of the input file from the page cache
#ifdef __linux__
        posix_fadvise(fd, 0, st.st_size, POSIX_FADV_DONTNEED);
#endif
        close(fd);

        auto start_writing = std::chrono::high_resolution_clock::now();
        if (min_abundance > 0)
            hash_table->write_kmers_on_disk_separately_even_faster(min_abundance, output_file);
        auto end_writing = std::chrono::high_resolution_clock::now();
        delete hash_table;

        if (print_times)
        {
            auto build_duration = std::chrono::duration_cast<std::chrono::microseconds>(start_writing - start_building);
            auto writing_duration = std::chrono::duration_cast<std::chrono::microseconds>(end_writing - start_writing);
            std::cout << "Time used to build hash table: " << build_duration.count() << " microseconds\n";
            std::cout << "Time used to write k-mers in a file: " << writing_duration.count() << " microseconds\n";
        }
        
    }
    
};

#endif //PARALLEL_PARSING_PARALLEL_PARSER_HPP
