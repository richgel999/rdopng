// basisu.h
// Copyright (C) 2019-2021 Binomial LLC. All Rights Reserved.
// Important: If compiling with gcc, be sure strict aliasing is disabled: -fno-strict-aliasing
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once

#ifdef _MSC_VER

	#pragma warning (disable : 4201)
	#pragma warning (disable : 4127) // warning C4127: conditional expression is constant
	#pragma warning (disable : 4530) // C++ exception handler used, but unwind semantics are not enabled.
	
#endif // _MSC_VER

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>
#include <memory.h>
#include <limits.h>
#include <stdint.h>

#include <algorithm>
#include <limits>
#include <functional>
#include <iterator>
#include <type_traits>
#include <assert.h>
#include <random>

#include "basisu_containers.h"

#ifdef max
#undef max
#endif

#ifdef min
#undef min
#endif

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

// Set to one to enable debug printf()'s when any errors occur, for development/debugging. Especially useful for WebGL development.
#ifndef BASISU_FORCE_DEVEL_MESSAGES
#define BASISU_FORCE_DEVEL_MESSAGES 0
#endif

#define BASISU_NOTE_UNUSED(x) (void)(x)
#define BASISU_ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#define BASISU_NO_EQUALS_OR_COPY_CONSTRUCT(x) x(const x &) = delete; x& operator= (const x &) = delete;
#define BASISU_ASSUME(x) static_assert(x, #x);
#define BASISU_OFFSETOF(s, m) offsetof(s, m)
#define BASISU_STRINGIZE(x) #x
#define BASISU_STRINGIZE2(x) BASISU_STRINGIZE(x)

#if BASISU_FORCE_DEVEL_MESSAGES
	#define BASISU_DEVEL_ERROR(...) do { basisu::debug_printf(__VA_ARGS__); } while(0)
#else
	#define BASISU_DEVEL_ERROR(...)
#endif

namespace basisu
{
	// Types/utilities

#ifdef _WIN32
	const char BASISU_PATH_SEPERATOR_CHAR = '\\';
#else
	const char BASISU_PATH_SEPERATOR_CHAR = '/';
#endif

	typedef basisu::vector<uint8_t> uint8_vec;
	typedef basisu::vector<int16_t> int16_vec;
	typedef basisu::vector<uint16_t> uint16_vec;
	typedef basisu::vector<uint32_t> uint_vec;
	typedef basisu::vector<uint64_t> uint64_vec;
	typedef basisu::vector<int> int_vec;
	typedef basisu::vector<bool> bool_vec;

	void enable_debug_printf(bool enabled);
	void debug_printf(const char *pFmt, ...);
		
	template <typename T> inline void clear_obj(T& obj) { memset(&obj, 0, sizeof(obj)); }

	template <typename T0, typename T1> inline T0 lerp(T0 a, T0 b, T1 c) { return a + (b - a) * c; }

	template <typename S> inline S maximum(S a, S b) { return (a > b) ? a : b; }
	template <typename S> inline S maximum(S a, S b, S c) { return maximum(maximum(a, b), c); }
	template <typename S> inline S maximum(S a, S b, S c, S d) { return maximum(maximum(maximum(a, b), c), d); }
	
	template <typename S> inline S minimum(S a, S b) {	return (a < b) ? a : b; }
	template <typename S> inline S minimum(S a, S b, S c) {	return minimum(minimum(a, b), c); }
	template <typename S> inline S minimum(S a, S b, S c, S d) { return minimum(minimum(minimum(a, b), c), d); }

	inline float clampf(float value, float low, float high) { if (value < low) value = low; else if (value > high) value = high;	return value; }
	inline float saturate(float value) { return clampf(value, 0, 1.0f); }
	inline uint8_t minimumub(uint8_t a, uint8_t b) { return (a < b) ? a : b; }
	inline uint32_t minimumu(uint32_t a, uint32_t b) { return (a < b) ? a : b; }
	inline int32_t minimumi(int32_t a, int32_t b) { return (a < b) ? a : b; }
	inline float minimumf(float a, float b) { return (a < b) ? a : b; }
	inline uint8_t maximumub(uint8_t a, uint8_t b) { return (a > b) ? a : b; }
	inline uint32_t maximumu(uint32_t a, uint32_t b) { return (a > b) ? a : b; }
	inline int32_t maximumi(int32_t a, int32_t b) { return (a > b) ? a : b; }
	inline float maximumf(float a, float b) { return (a > b) ? a : b; }
	inline int squarei(int i) { return i * i; }
	inline float squaref(float i) { return i * i; }
	template<typename T> inline T square(T a) { return a * a; }

	template <typename S> inline S clamp(S value, S low, S high) { return (value < low) ? low : ((value > high) ? high : value); }

	inline uint32_t iabs(int32_t i) { return (i < 0) ? static_cast<uint32_t>(-i) : static_cast<uint32_t>(i);	}
	inline uint64_t iabs64(int64_t i) {	return (i < 0) ? static_cast<uint64_t>(-i) : static_cast<uint64_t>(i); }

	template<typename T> inline void clear_vector(T &vec) { vec.erase(vec.begin(), vec.end()); }		
	template<typename T> inline typename T::value_type *enlarge_vector(T &vec, size_t n) { size_t cs = vec.size(); vec.resize(cs + n); return &vec[cs]; }

	inline bool is_pow2(uint32_t x) { return x && ((x & (x - 1U)) == 0U); }
	inline bool is_pow2(uint64_t x) { return x && ((x & (x - 1U)) == 0U); }

	template<typename T> inline T open_range_check(T v, T minv, T maxv) { assert(v >= minv && v < maxv); BASISU_NOTE_UNUSED(minv); BASISU_NOTE_UNUSED(maxv); return v; }
	template<typename T> inline T open_range_check(T v, T maxv) { assert(v < maxv); BASISU_NOTE_UNUSED(maxv); return v; }

	inline uint32_t total_bits(uint32_t v) { uint32_t l = 0; for ( ; v > 0U; ++l) v >>= 1; return l; }

	template<typename T> inline T saturate(T val) { return clamp(val, 0.0f, 1.0f); }

	template<typename T, typename R> inline void append_vector(T &vec, const R *pObjs, size_t n) 
	{ 
		if (n)
		{
			if (vec.size())
			{
				assert((pObjs + n) <= vec.begin() || (pObjs >= vec.end()));
			}
			const size_t cur_s = vec.size();
			vec.resize(cur_s + n);
			memcpy(&vec[cur_s], pObjs, sizeof(R) * n);
		}
	}

	template<typename T> inline void append_vector(T &vec, const T &other_vec)
	{
		assert(&vec != &other_vec);
		if (other_vec.size())
			append_vector(vec, &other_vec[0], other_vec.size());
	}

	template<typename T> inline void vector_ensure_element_is_valid(T &vec, size_t idx)
	{
		if (idx >= vec.size())
			vec.resize(idx + 1);
	}

	template<typename T> inline void vector_sort(T &vec)
	{
		if (vec.size())
			std::sort(vec.begin(), vec.end());
	}

	template<typename T, typename U> inline bool unordered_set_contains(T& set, const U&obj)
	{
		return set.find(obj) != set.end();
	}

	template<typename T> int vector_find(const T &vec, const typename T::value_type &obj)
	{
		assert(vec.size() <= INT_MAX);
		for (size_t i = 0; i < vec.size(); i++)
			if (vec[i] == obj)
				return static_cast<int>(i);
		return -1;
	}

	template<typename T> void vector_set_all(T &vec, const typename T::value_type &obj)
	{
		for (size_t i = 0; i < vec.size(); i++)
			vec[i] = obj;
	}
		
	inline uint64_t read_be64(const void *p)
	{
		uint64_t val = 0;
		for (uint32_t i = 0; i < 8; i++)
			val |= (static_cast<uint64_t>(static_cast<const uint8_t *>(p)[7 - i]) << (i * 8));
		return val;
	}

	inline void write_be64(void *p, uint64_t x)
	{
		for (uint32_t i = 0; i < 8; i++)
			static_cast<uint8_t *>(p)[7 - i] = static_cast<uint8_t>(x >> (i * 8));
	}

	static inline uint16_t byteswap16(uint16_t x) { return static_cast<uint16_t>((x << 8) | (x >> 8)); }
	static inline uint32_t byteswap32(uint32_t x) { return ((x << 24) | ((x << 8) & 0x00FF0000) | ((x >> 8) & 0x0000FF00) | (x >> 24)); }

	inline uint32_t floor_log2i(uint32_t v)
	{
		uint32_t b = 0;
		for (; v > 1U; ++b)
			v >>= 1;
		return b;
	}

	inline uint32_t ceil_log2i(uint32_t v)
	{
		uint32_t b = floor_log2i(v);
		if ((b != 32) && (v > (1U << b)))
			++b;
		return b;
	}

	inline int posmod(int x, int y)
	{
		if (x >= 0)
			return (x < y) ? x : (x % y);
		int m = (-x) % y;
		return (m != 0) ? (y - m) : m;
	}

	inline bool do_excl_ranges_overlap(int la, int ha, int lb, int hb)
	{
		assert(la < ha && lb < hb);
		if ((ha <= lb) || (la >= hb)) return false;
		return true;
	}

	static inline uint32_t read_le_dword(const uint8_t *pBytes)
	{
		return (pBytes[3] << 24U) | (pBytes[2] << 16U) | (pBytes[1] << 8U) | (pBytes[0]);
	}

	static inline void write_le_dword(uint8_t* pBytes, uint32_t val)
	{
		pBytes[0] = (uint8_t)val;
		pBytes[1] = (uint8_t)(val >> 8U);
		pBytes[2] = (uint8_t)(val >> 16U);
		pBytes[3] = (uint8_t)(val >> 24U);
	}
		
	// Always little endian 1-8 byte unsigned int
	template<uint32_t NumBytes>
	struct packed_uint
	{
		uint8_t m_bytes[NumBytes];

		inline packed_uint() { static_assert(NumBytes <= sizeof(uint64_t), "Invalid NumBytes"); }
		inline packed_uint(uint64_t v) { *this = v; }
		inline packed_uint(const packed_uint& other) { *this = other; }
						
		inline packed_uint& operator= (uint64_t v) 
		{ 
			for (uint32_t i = 0; i < NumBytes; i++) 
				m_bytes[i] = static_cast<uint8_t>(v >> (i * 8)); 
			return *this; 
		}

		inline packed_uint& operator= (const packed_uint& rhs) 
		{ 
			memcpy(m_bytes, rhs.m_bytes, sizeof(m_bytes)); 
			return *this;
		}

		inline operator uint32_t() const
		{
			switch (NumBytes)
			{
				case 1:  
				{
					return  m_bytes[0];
				}
				case 2:  
				{
					return (m_bytes[1] << 8U) | m_bytes[0];
				}
				case 3:  
				{
					return (m_bytes[2] << 16U) | (m_bytes[1] << 8U) | m_bytes[0];
				}
				case 4:  
				{
					return read_le_dword(m_bytes);
				}
				case 5:
				{
					uint32_t l = read_le_dword(m_bytes);
					uint32_t h = m_bytes[4];
					return static_cast<uint64_t>(l) | (static_cast<uint64_t>(h) << 32U);
				}
				case 6:
				{
					uint32_t l = read_le_dword(m_bytes);
					uint32_t h = (m_bytes[5] << 8U) | m_bytes[4];
					return static_cast<uint64_t>(l) | (static_cast<uint64_t>(h) << 32U);
				}
				case 7:
				{
					uint32_t l = read_le_dword(m_bytes);
					uint32_t h = (m_bytes[6] << 16U) | (m_bytes[5] << 8U) | m_bytes[4];
					return static_cast<uint64_t>(l) | (static_cast<uint64_t>(h) << 32U);
				}
				case 8:  
				{
					uint32_t l = read_le_dword(m_bytes);
					uint32_t h = read_le_dword(m_bytes + 4);
					return static_cast<uint64_t>(l) | (static_cast<uint64_t>(h) << 32U);
				}
				default: 
				{
					assert(0);
					return 0;
				}
			}
		}
	};

	enum eZero { cZero };
	enum eNoClamp { cNoClamp };
	
	// Rice/Huffman entropy coding
		
	// This is basically Deflate-style canonical Huffman, except we allow for a lot more symbols.
	enum
	{
		cHuffmanMaxSupportedCodeSize = 16, cHuffmanMaxSupportedInternalCodeSize = 31, 
		cHuffmanFastLookupBits = 10, 
		cHuffmanMaxSymsLog2 = 14, cHuffmanMaxSyms = 1 << cHuffmanMaxSymsLog2,

		// Small zero runs
		cHuffmanSmallZeroRunSizeMin = 3, cHuffmanSmallZeroRunSizeMax = 10, cHuffmanSmallZeroRunExtraBits = 3,

		// Big zero run
		cHuffmanBigZeroRunSizeMin = 11, cHuffmanBigZeroRunSizeMax = 138, cHuffmanBigZeroRunExtraBits = 7,

		// Small non-zero run
		cHuffmanSmallRepeatSizeMin = 3, cHuffmanSmallRepeatSizeMax = 6, cHuffmanSmallRepeatExtraBits = 2,

		// Big non-zero run
		cHuffmanBigRepeatSizeMin = 7, cHuffmanBigRepeatSizeMax = 134, cHuffmanBigRepeatExtraBits = 7,

		cHuffmanTotalCodelengthCodes = 21, cHuffmanSmallZeroRunCode = 17, cHuffmanBigZeroRunCode = 18, cHuffmanSmallRepeatCode = 19, cHuffmanBigRepeatCode = 20
	};

	static const uint8_t g_huffman_sorted_codelength_codes[] = { cHuffmanSmallZeroRunCode, cHuffmanBigZeroRunCode,	cHuffmanSmallRepeatCode, cHuffmanBigRepeatCode, 0, 8, 7, 9, 6, 0xA, 5, 0xB, 4, 0xC, 3, 0xD, 2, 0xE, 1, 0xF, 0x10 };
	const uint32_t cHuffmanTotalSortedCodelengthCodes = sizeof(g_huffman_sorted_codelength_codes) / sizeof(g_huffman_sorted_codelength_codes[0]);
							
} // namespace basisu

