// basisu_transcoder_internal.h - Universal texture format transcoder library.
// Copyright (C) 2019-2021 Binomial LLC. All Rights Reserved.
//
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
#pragma warning (disable: 4127) //  conditional expression is constant
#endif

#define BASISD_LIB_VERSION 116
#define BASISD_VERSION_STRING "01.16"

#ifdef _DEBUG
#define BASISD_BUILD_DEBUG
#else
#define BASISD_BUILD_RELEASE
#endif

#include "basisu.h"

#define BASISD_znew (z = 36969 * (z & 65535) + (z >> 16))

namespace basisu
{
	extern bool g_debug_printf;
}

namespace basist
{
	class huffman_decoding_table
	{
		friend class bitwise_decoder;

	public:
		huffman_decoding_table()
		{
		}

		void clear()
		{
			basisu::clear_vector(m_code_sizes);
			basisu::clear_vector(m_lookup);
			basisu::clear_vector(m_tree);
		}

		bool init(uint32_t total_syms, const uint8_t *pCode_sizes, uint32_t fast_lookup_bits = basisu::cHuffmanFastLookupBits)
		{
			if (!total_syms)
			{
				clear();
				return true;
			}

			m_code_sizes.resize(total_syms);
			memcpy(&m_code_sizes[0], pCode_sizes, total_syms);

			const uint32_t huffman_fast_lookup_size = 1 << fast_lookup_bits;

			m_lookup.resize(0);
			m_lookup.resize(huffman_fast_lookup_size);

			m_tree.resize(0);
			m_tree.resize(total_syms * 2);

			uint32_t syms_using_codesize[basisu::cHuffmanMaxSupportedInternalCodeSize + 1];
			basisu::clear_obj(syms_using_codesize);
			for (uint32_t i = 0; i < total_syms; i++)
			{
				if (pCode_sizes[i] > basisu::cHuffmanMaxSupportedInternalCodeSize)
					return false;
				syms_using_codesize[pCode_sizes[i]]++;
			}

			uint32_t next_code[basisu::cHuffmanMaxSupportedInternalCodeSize + 1];
			next_code[0] = next_code[1] = 0;

			uint32_t used_syms = 0, total = 0;
			for (uint32_t i = 1; i < basisu::cHuffmanMaxSupportedInternalCodeSize; i++)
			{
				used_syms += syms_using_codesize[i];
				next_code[i + 1] = (total = ((total + syms_using_codesize[i]) << 1));
			}

			if (((1U << basisu::cHuffmanMaxSupportedInternalCodeSize) != total) && (used_syms > 1U))
				return false;

			for (int tree_next = -1, sym_index = 0; sym_index < (int)total_syms; ++sym_index)
			{
				uint32_t rev_code = 0, l, cur_code, code_size = pCode_sizes[sym_index];
				if (!code_size)
					continue;

				cur_code = next_code[code_size]++;

				for (l = code_size; l > 0; l--, cur_code >>= 1)
					rev_code = (rev_code << 1) | (cur_code & 1);

				if (code_size <= fast_lookup_bits)
				{
					uint32_t k = (code_size << 16) | sym_index;
					while (rev_code < huffman_fast_lookup_size)
					{
						if (m_lookup[rev_code] != 0)
						{
							// Supplied codesizes can't create a valid prefix code.
							return false;
						}

						m_lookup[rev_code] = k;
						rev_code += (1 << code_size);
					}
					continue;
				}

				int tree_cur;
				if (0 == (tree_cur = m_lookup[rev_code & (huffman_fast_lookup_size - 1)]))
				{
					const uint32_t idx = rev_code & (huffman_fast_lookup_size - 1);
					if (m_lookup[idx] != 0)
					{
						// Supplied codesizes can't create a valid prefix code.
						return false;
					}

					m_lookup[idx] = tree_next;
					tree_cur = tree_next;
					tree_next -= 2;
				}

				if (tree_cur >= 0)
				{
					// Supplied codesizes can't create a valid prefix code.
					return false;
				}

				rev_code >>= (fast_lookup_bits - 1);

				for (int j = code_size; j > ((int)fast_lookup_bits + 1); j--)
				{
					tree_cur -= ((rev_code >>= 1) & 1);

					const int idx = -tree_cur - 1;
					if (idx < 0)
						return false;
					else if (idx >= (int)m_tree.size())
						m_tree.resize(idx + 1);
										
					if (!m_tree[idx])
					{
						m_tree[idx] = (int16_t)tree_next;
						tree_cur = tree_next;
						tree_next -= 2;
					}
					else
					{
						tree_cur = m_tree[idx];
						if (tree_cur >= 0)
						{
							// Supplied codesizes can't create a valid prefix code.
							return false;
						}
					}
				}

				tree_cur -= ((rev_code >>= 1) & 1);

				const int idx = -tree_cur - 1;
				if (idx < 0)
					return false;
				else if (idx >= (int)m_tree.size())
					m_tree.resize(idx + 1);

				if (m_tree[idx] != 0)
				{
					// Supplied codesizes can't create a valid prefix code.
					return false;
				}

				m_tree[idx] = (int16_t)sym_index;
			}

			return true;
		}

		const basisu::uint8_vec &get_code_sizes() const { return m_code_sizes; }
		const basisu::int_vec get_lookup() const { return m_lookup; }
		const basisu::int16_vec get_tree() const { return m_tree; }

		bool is_valid() const { return m_code_sizes.size() > 0; }

	private:
		basisu::uint8_vec m_code_sizes;
		basisu::int_vec m_lookup;
		basisu::int16_vec m_tree;
	};

	class bitwise_decoder
	{
	public:
		bitwise_decoder() :
			m_buf_size(0),
			m_pBuf(nullptr),
			m_pBuf_start(nullptr),
			m_pBuf_end(nullptr),
			m_bit_buf(0),
			m_bit_buf_size(0)
		{
		}

		void clear()
		{
			m_buf_size = 0;
			m_pBuf = nullptr;
			m_pBuf_start = nullptr;
			m_pBuf_end = nullptr;
			m_bit_buf = 0;
			m_bit_buf_size = 0;
		}

		bool init(const uint8_t *pBuf, uint32_t buf_size)
		{
			if ((!pBuf) && (buf_size))
				return false;

			m_buf_size = buf_size;
			m_pBuf = pBuf;
			m_pBuf_start = pBuf;
			m_pBuf_end = pBuf + buf_size;
			m_bit_buf = 0;
			m_bit_buf_size = 0;
			return true;
		}

		void stop()
		{
		}

		inline uint32_t peek_bits(uint32_t num_bits)
		{
			if (!num_bits)
				return 0;

			assert(num_bits <= 25);

			while (m_bit_buf_size < num_bits)
			{
				uint32_t c = 0;
				if (m_pBuf < m_pBuf_end)
					c = *m_pBuf++;

				m_bit_buf |= (c << m_bit_buf_size);
				m_bit_buf_size += 8;
				assert(m_bit_buf_size <= 32);
			}

			return m_bit_buf & ((1 << num_bits) - 1);
		}

		void remove_bits(uint32_t num_bits)
		{
			assert(m_bit_buf_size >= num_bits);

			m_bit_buf >>= num_bits;
			m_bit_buf_size -= num_bits;
		}

		uint32_t get_bits(uint32_t num_bits)
		{
			if (num_bits > 25)
			{
				assert(num_bits <= 32);

				const uint32_t bits0 = peek_bits(25);
				m_bit_buf >>= 25;
				m_bit_buf_size -= 25;
				num_bits -= 25;

				const uint32_t bits = peek_bits(num_bits);
				m_bit_buf >>= num_bits;
				m_bit_buf_size -= num_bits;

				return bits0 | (bits << 25);
			}

			const uint32_t bits = peek_bits(num_bits);

			m_bit_buf >>= num_bits;
			m_bit_buf_size -= num_bits;

			return bits;
		}

		uint32_t decode_truncated_binary(uint32_t n)
		{
			assert(n >= 2);

			const uint32_t k = basisu::floor_log2i(n);
			const uint32_t u = (1 << (k + 1)) - n;

			uint32_t result = get_bits(k);

			if (result >= u)
				result = ((result << 1) | get_bits(1)) - u;

			return result;
		}

		uint32_t decode_rice(uint32_t m)
		{
			assert(m);

			uint32_t q = 0;
			for (;;)
			{
				uint32_t k = peek_bits(16);
				
				uint32_t l = 0;
				while (k & 1)
				{
					l++;
					k >>= 1;
				}
				
				q += l;

				remove_bits(l);

				if (l < 16)
					break;
			}

			return (q << m) + (get_bits(m + 1) >> 1);
		}

		inline uint32_t decode_vlc(uint32_t chunk_bits)
		{
			assert(chunk_bits);

			const uint32_t chunk_size = 1 << chunk_bits;
			const uint32_t chunk_mask = chunk_size - 1;
					
			uint32_t v = 0;
			uint32_t ofs = 0;

			for ( ; ; )
			{
				uint32_t s = get_bits(chunk_bits + 1);
				v |= ((s & chunk_mask) << ofs);
				ofs += chunk_bits;

				if ((s & chunk_size) == 0)
					break;
				
				if (ofs >= 32)
				{
					assert(0);
					break;
				}
			}

			return v;
		}

		inline uint32_t decode_huffman(const huffman_decoding_table &ct, int fast_lookup_bits = basisu::cHuffmanFastLookupBits)
		{
			assert(ct.m_code_sizes.size());

			const uint32_t huffman_fast_lookup_size = 1 << fast_lookup_bits;
						
			while (m_bit_buf_size < 16)
			{
				uint32_t c = 0;
				if (m_pBuf < m_pBuf_end)
					c = *m_pBuf++;

				m_bit_buf |= (c << m_bit_buf_size);
				m_bit_buf_size += 8;
				assert(m_bit_buf_size <= 32);
			}
						
			int code_len;

			int sym;
			if ((sym = ct.m_lookup[m_bit_buf & (huffman_fast_lookup_size - 1)]) >= 0)
			{
				code_len = sym >> 16;
				sym &= 0xFFFF;
			}
			else
			{
				code_len = fast_lookup_bits;
				do
				{
					sym = ct.m_tree[~sym + ((m_bit_buf >> code_len++) & 1)]; // ~sym = -sym - 1
				} while (sym < 0);
			}

			m_bit_buf >>= code_len;
			m_bit_buf_size -= code_len;

			return sym;
		}

		bool read_huffman_table(huffman_decoding_table &ct)
		{
			ct.clear();

			const uint32_t total_used_syms = get_bits(basisu::cHuffmanMaxSymsLog2);

			if (!total_used_syms)
				return true;
			if (total_used_syms > basisu::cHuffmanMaxSyms)
				return false;

			uint8_t code_length_code_sizes[basisu::cHuffmanTotalCodelengthCodes];
			basisu::clear_obj(code_length_code_sizes);

			const uint32_t num_codelength_codes = get_bits(5);
			if ((num_codelength_codes < 1) || (num_codelength_codes > basisu::cHuffmanTotalCodelengthCodes))
				return false;

			for (uint32_t i = 0; i < num_codelength_codes; i++)
				code_length_code_sizes[basisu::g_huffman_sorted_codelength_codes[i]] = static_cast<uint8_t>(get_bits(3));

			huffman_decoding_table code_length_table;
			if (!code_length_table.init(basisu::cHuffmanTotalCodelengthCodes, code_length_code_sizes))
				return false;

			if (!code_length_table.is_valid())
				return false;

			basisu::uint8_vec code_sizes(total_used_syms);

			uint32_t cur = 0;
			while (cur < total_used_syms)
			{
				int c = decode_huffman(code_length_table);

				if (c <= 16)
					code_sizes[cur++] = static_cast<uint8_t>(c);
				else if (c == basisu::cHuffmanSmallZeroRunCode)
					cur += get_bits(basisu::cHuffmanSmallZeroRunExtraBits) + basisu::cHuffmanSmallZeroRunSizeMin;
				else if (c == basisu::cHuffmanBigZeroRunCode)
					cur += get_bits(basisu::cHuffmanBigZeroRunExtraBits) + basisu::cHuffmanBigZeroRunSizeMin;
				else
				{
					if (!cur)
						return false;

					uint32_t l;
					if (c == basisu::cHuffmanSmallRepeatCode)
						l = get_bits(basisu::cHuffmanSmallRepeatExtraBits) + basisu::cHuffmanSmallRepeatSizeMin;
					else
						l = get_bits(basisu::cHuffmanBigRepeatExtraBits) + basisu::cHuffmanBigRepeatSizeMin;

					const uint8_t prev = code_sizes[cur - 1];
					if (prev == 0)
						return false;
					do
					{
						if (cur >= total_used_syms)
							return false;
						code_sizes[cur++] = prev;
					} while (--l > 0);
				}
			}

			if (cur != total_used_syms)
				return false;

			return ct.init(total_used_syms, &code_sizes[0]);
		}

	private:
		uint32_t m_buf_size;
		const uint8_t *m_pBuf;
		const uint8_t *m_pBuf_start;
		const uint8_t *m_pBuf_end;

		uint32_t m_bit_buf;
		uint32_t m_bit_buf_size;
	};

	inline uint32_t basisd_rand(uint32_t seed)
	{
		if (!seed)
			seed++;
		uint32_t z = seed;
		BASISD_znew;
		return z;
	}

	// Returns random number in [0,limit). Max limit is 0xFFFF.
	inline uint32_t basisd_urand(uint32_t& seed, uint32_t limit)
	{
		seed = basisd_rand(seed);
		return (((seed ^ (seed >> 16)) & 0xFFFF) * limit) >> 16;
	}

	class approx_move_to_front
	{
	public:
		approx_move_to_front(uint32_t n)
		{
			init(n);
		}

		void init(uint32_t n)
		{
			m_values.resize(n);
			m_rover = n / 2;
		}

		const basisu::int_vec& get_values() const { return m_values; }
		basisu::int_vec& get_values() { return m_values; }

		uint32_t size() const { return (uint32_t)m_values.size(); }

		const int& operator[] (uint32_t index) const { return m_values[index]; }
		int operator[] (uint32_t index) { return m_values[index]; }

		void add(int new_value)
		{
			m_values[m_rover++] = new_value;
			if (m_rover == m_values.size())
				m_rover = (uint32_t)m_values.size() / 2;
		}

		void use(uint32_t index)
		{
			if (index)
			{
				//std::swap(m_values[index / 2], m_values[index]);
				int x = m_values[index / 2];
				int y = m_values[index];
				m_values[index / 2] = y;
				m_values[index] = x;
			}
		}

		// returns -1 if not found
		int find(int value) const
		{
			for (uint32_t i = 0; i < m_values.size(); i++)
				if (m_values[i] == value)
					return i;
			return -1;
		}

		void reset()
		{
			const uint32_t n = (uint32_t)m_values.size();

			m_values.clear();

			init(n);
		}

	private:
		basisu::int_vec m_values;
		uint32_t m_rover;
	};

	struct decoder_etc_block;
	
	inline uint8_t clamp255(int32_t i)
	{
		return (uint8_t)((i & 0xFFFFFF00U) ? (~(i >> 31)) : i);
	}

	enum eNoClamp
	{
		cNoClamp = 0
	};
						
} // namespace basist



