#include "hash_functions.hpp"


RollingHasherDual::RollingHasherDual(uint64_t q, uint64_t m)
{
    // q = mod value
    // m = max power
    // d = alphabet size (for multiplication)
    // di = modular multiplicative inverse of d (for division)
    this->q = q;
    this->rq = 0;
    this->m = m;
    this->d = 5;
    this->di = mathfunctions::modular_multiplicative_inverse_coprimes(d, q);
    this->tbm = false;
    //std::cout << "Hash table size: " << q << "\n";
    //std::cout << "Modular multiplicative inverse: " << this->di << "\n";
    bpc = 2;
    character_mask = 3ULL;
    current_hash_forward = 0;
    current_hash_backward = 0;
    hashed_count = 0;
    h = 1;
    for (uint64_t i = 0; i < m-1; i++)
        h = (h*d)%q;
}

RollingHasherDual::RollingHasherDual(uint64_t q, uint64_t m, uint64_t modular_multiplicative_inverse, uint64_t multiplier)
{
    // q = mod value
    // m = max power
    // d = alphabet size (for multiplication)
    // di = modular multiplicative inverse of d (for division)
    this->q = q;
    this->rq = 0;
    this->m = m;
    this->d = multiplier;
    this->di = modular_multiplicative_inverse;
    this->tbm = false;
    //std::cout << "Hash table size: " << q << "\n";
    //std::cout << "Modular multiplicative inverse: " << this->di << "\n";
    bpc = 2;
    character_mask = 3ULL;
    current_hash_forward = 0;
    current_hash_backward = 0;
    hashed_count = 0;
    h = 1;
    for (uint64_t i = 0; i < m-1; i++)
        h = (h*d)%q;
}

RollingHasherDual::RollingHasherDual(uint64_t q, uint64_t m, uint64_t modular_multiplicative_inverse, uint64_t multiplier, uint64_t return_q)
{
    // q = mod value
    // m = max power
    // d = alphabet size (for multiplication)
    // di = modular multiplicative inverse of d (for division)
    this->q = q;
    this->rq = return_q;
    this->m = m;
    this->d = multiplier;
    this->di = modular_multiplicative_inverse;
    this->tbm = false;
    //std::cout << "Hash table size: " << q << "\n";
    //std::cout << "Modular multiplicative inverse: " << this->di << "\n";
    bpc = 2;
    character_mask = 3ULL;
    current_hash_forward = 0;
    current_hash_backward = 0;
    hashed_count = 0;
    h = 1;
    for (uint64_t i = 0; i < m-1; i++)
        h = (h*d)%q;

}

RollingHasherDual::RollingHasherDual(uint64_t q, uint64_t m, uint64_t modular_multiplicative_inverse, uint64_t multiplier, uint64_t return_q, bool twobitmod)
{
    // q = mod value
    // m = max power
    // d = alphabet size (for multiplication)
    // di = modular multiplicative inverse of d (for division)
    this->q = q;
    this->rq = return_q;
    this->m = m;
    this->d = multiplier;
    this->di = modular_multiplicative_inverse;
    this->tbm = twobitmod;
    //std::cout << "Hash table size: " << q << "\n";
    //std::cout << "Modular multiplicative inverse: " << this->di << "\n";
    bpc = 2;
    character_mask = 3ULL;
    current_hash_forward = 0;
    current_hash_backward = 0;
    hashed_count = 0;
    h = 1;
    for (uint64_t i = 0; i < m-1; i++)
        h = (h*d)%q;

}

void RollingHasherDual::update_rolling_hash_in(uint64_t in)
{
    // If mod is power of 2, use this
    if (tbm){
        current_hash_forward = (d*current_hash_forward + in) & (q-1);
        // Alternative reverse update version
        uint64_t reverse_add = twobitstringfunctions::reverse_int(in);
        for (uint64_t i = 0; i < hashed_count; i++)
            reverse_add = (reverse_add*d) & (q-1);
        current_hash_backward = (current_hash_backward + reverse_add) & (q-1);

    } else {
        current_hash_forward = (d*current_hash_forward + in) % q;
        // Alternative reverse update version
        uint64_t reverse_add = twobitstringfunctions::reverse_int(in);
        for (uint64_t i = 0; i < hashed_count; i++)
            reverse_add = (reverse_add*d) % q;
        current_hash_backward = (current_hash_backward + reverse_add) % q;
        // Original reverse update version
        //current_hash_backward = (di*current_hash_backward + twobitstringfunctions::reverse_int(in)*h) % q;
    }
    
}

// Should be fine now
void RollingHasherDual::update_rolling_hash_in_and_out(uint64_t in, uint64_t out)
{
    if (tbm){
        //std::cout << "INSERTING " << in << " AND OUTING " << out << "\n";
        // First update forward
        __uint128_t baseline = (d*current_hash_forward + in) & (q-1);
        __uint128_t to_be_removed = (d*h*out) & (q-1);
        if (to_be_removed > baseline)
        {
            current_hash_forward = uint64_t((baseline + q) - to_be_removed);
        }
        else
        {
            current_hash_forward = uint64_t(baseline - to_be_removed);
        }
        // Next update backward
        baseline = current_hash_backward;
        //std::cout << "Baseline is: " << baseline << "\n";
        to_be_removed = twobitstringfunctions::reverse_int(out);
        //std::cout << "To be removed is: " << to_be_removed << "\n";
        if (to_be_removed > baseline)
        {
            baseline = ((baseline + q) - to_be_removed);
        }
        else
        {
            baseline = baseline - to_be_removed;
        }
        // Simulate division with inverse multiplication
        baseline = (baseline * di) & (q-1);
        current_hash_backward = uint64_t(((twobitstringfunctions::reverse_int(in)*h) + baseline) & (q-1));

        //hashed_count += 1;
    } else {
        //std::cout << "INSERTING " << in << " AND OUTING " << out << "\n";
        // First update forward
        __uint128_t baseline = (d*current_hash_forward + in) % q;
        __uint128_t to_be_removed = (d*h*out) % q;
        if (to_be_removed > baseline)
        {
            current_hash_forward = uint64_t((baseline + q) - to_be_removed);
        }
        else
        {
            current_hash_forward = uint64_t(baseline - to_be_removed);
        }
        // Next update backward
        baseline = current_hash_backward;
        //std::cout << "Baseline is: " << baseline << "\n";
        to_be_removed = twobitstringfunctions::reverse_int(out);
        //std::cout << "To be removed is: " << to_be_removed << "\n";
        if (to_be_removed > baseline)
        {
            baseline = ((baseline + q) - to_be_removed);
        }
        else
        {
            baseline = baseline - to_be_removed;
        }
        // Simulate division with inverse multiplication
        baseline = (baseline * di) % q;
        current_hash_backward = uint64_t(((twobitstringfunctions::reverse_int(in)*h) + baseline) % q);
        //hashed_count += 1;
    }
    
}

void RollingHasherDual::update_rolling_hash(uint64_t in, uint64_t out)
{
    // Update number of characters in the hash value
    // Based on the outgoing character and hash count, update hash value accordingly
    if (hashed_count < m)
    {
        //std::cout << "ONLY IN HASHING\n";
        update_rolling_hash_in(in);
    }
    else
    {
        update_rolling_hash_in_and_out(in, out);
    }
    hashed_count = std::min(m, hashed_count+1);
}

uint64_t RollingHasherDual::get_current_hash_forward()
{
    if (rq == 0)
        return uint64_t(current_hash_forward);
    return uint64_t(current_hash_forward % rq);
}

uint64_t RollingHasherDual::get_current_hash_backward()
{
    if (rq == 0)
        return uint64_t(current_hash_backward);
    return uint64_t(current_hash_backward % rq);
}

uint64_t RollingHasherDual::get_current_hash_forward_rqless()
{
    return uint64_t(current_hash_forward);
}

uint64_t RollingHasherDual::get_current_hash_backward_rqless()
{
    return uint64_t(current_hash_backward);
}


void RollingHasherDual::reset()
{
    current_hash_forward = 0;
    current_hash_backward = 0;
    hashed_count = 0;
}

// Only factory loads remain to be implemented plus check thatthe factories are looking good with the new idea
void RollingHasherDual::load_full_factory_canonical(KMerFactoryCanonical2BC * kmer_factory)
{
    int current_block_position = 0;
    uint64_t current_block_forward = kmer_factory->blocks_forward[current_block_position];
    int bits_already_read_from_current_block = 64-kmer_factory->bits_in_last_block;
    current_block_forward = current_block_forward << (bits_already_read_from_current_block);

    for (int i = 0; i < kmer_factory->get_number_of_stored_characters(); i++)
    {
        update_rolling_hash_in(((current_block_forward >> (64 - 2)) & uint64_t(3)));
        current_block_forward = current_block_forward << 2;
        bits_already_read_from_current_block += 2;
        if (bits_already_read_from_current_block == 64)
        {
            current_block_position += 1;
            current_block_forward = kmer_factory->blocks_forward[current_block_position];
            bits_already_read_from_current_block = 0;
        }
    }
}


//##########################################################################################################
//##########################################################################################################


//##########################################################################################################
//##########################################################################################################


// Double hashing probing
uint64_t ProbeHasher1::probe_1(uint64_t item, uint64_t P)
{
    //return 1;
    return P - (item % P);
    //if (item > 0)
    //    return item % M;
    //return 1;
}

// Quadratic probing
uint64_t ProbeHasher1::probe_2(uint64_t iteration)
{
    return iteration * iteration;
}

// Linear probing
uint64_t ProbeHasher1::probe_3(uint64_t iteration)
{
    return 1;
}


// Better quadratic probing for prime N = 3 mod 4
uint64_t ProbeHasher1::probe_4(uint64_t iteration, uint64_t position, uint64_t modulo)
{
    // Try simple linear probing
    //return ((position+iteration) % modulo);
    uint64_t new_position = (position + iteration*iteration) % modulo;
    return new_position;

    /*
    uint64_t jump = (iteration*iteration) % modulo;
    uint64_t new_position = position;
    if (iteration % 2 == 0){
        new_position = (new_position + jump) % modulo;
    } else {
        if (jump <= position){
            new_position = (new_position - jump) % modulo;
        } else {
            new_position = modulo - (jump-position);
        }
    }
    return new_position;
    */
}