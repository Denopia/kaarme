#include <iostream>
#include <cstdint>

#pragma once

/*

    File containing some string functions,
    currently mainly conversions between
    string and integer forms

*/

// Pure string functions

namespace purestringfunctions
{
    static inline char reverse_char(char c)
    {
        if (c == 'A' || c == 'a'){return 'T';}
        if (c == 'C' || c == 'c'){return 'G';}
        if (c == 'G' || c == 'g'){return 'C';}
        if (c == 'T' || c == 't'){return 'A';}
        std::cerr << "** ERROR ** Invalid character wanted to be reversed in 'fun_strings.hpp': code 000\n";
        return 'Z';
    }

    static void reverse_this_string(std::string & s)
    {
        //std::cout << "File is being reversed\n";
        std::string ts(s);
        s = "";
        for (char c : ts)
            s = reverse_char(c) + s;
    }

    static inline std::string reverse_string(std::string & s)
    {
        std::string rs = "";
        for (char c : s)
            rs = reverse_char(c) + rs;
        return rs;
    }

    static inline int is_canonical(std::string & s)
    {
        for (int i = 0; i < s.length(); i++)
        {
            if (s.at(i) < reverse_char(s.at(s.length()-i-1)))
            {
                return 1;
            }
            else if (s.at(i) > reverse_char(s.at(s.length()-i-1)))
            {
                return -1;
            }
        }
        return 0;
    }
}



// 2 bit character functions
namespace twobitstringfunctions
{

    static inline uint64_t char2int(char c)
    {
        if (c == 'A' || c == 'a'){return 0ULL;}
        if (c == 'C' || c == 'c'){return 1ULL;}
        if (c == 'G' || c == 'g'){return 2ULL;}
        if (c == 'T' || c == 't'){return 3ULL;}
        std::cerr << "** ERROR ** Invalid character error in 'fun_strings.hpp': code 001\n";
        return 4ULL;
    }

    static inline char int2char(uint64_t c)
    {
        if (c == 0ULL){return 'A';}
        if (c == 1ULL){return 'C';}
        if (c == 2ULL){return 'G';}
        if (c == 3ULL){return 'T';}
        std::cerr << "** ERROR ** Invalid character error in 'fun_strings.hpp': code 002\n";
        return 'N';
    }

    static inline uint64_t reverse_int(uint64_t c)
    {
        return 3ULL - c;
    }

    static inline std::string int2string_single(uint64_t s, int l)
    {
        std::string your_string = "";
        for (int i = 0; i < l; i++)
        {
            your_string = int2char((s&3ULL)) + your_string;
            s >>= 2;
        }
        return your_string;
    }

    static inline std::string int2string_multi(uint64_t* d, int b, int l)
    {
        std::string your_string = "";
        int used = 0;
        for (int i = b-1; i >=0; i--)
        {
            your_string = int2string_single(d[i], std::min(32, l-used)) + your_string;
            used += 32;
            if (used >= l)
                break;
        }
        return your_string;
    }
    
    static inline bool is_clean_string(std::string s)
    {
        for (uint64_t i = 0; i < s.length(); i++)
        {
            if (char2int(s[i])>3ULL)
                return false;
        }
        return true;
    }

}