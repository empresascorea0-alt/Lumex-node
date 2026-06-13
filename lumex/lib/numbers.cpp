#include <lumex/crypto/blake2/blake2.h>
#include <lumex/crypto_lib/random_pool.hpp>
#include <lumex/crypto_lib/secure_memory.hpp>
#include <lumex/lib/balance_formatting.hpp>
#include <lumex/lib/numbers.hpp>
#include <lumex/lib/ratios.hpp>
#include <lumex/lib/utility.hpp>
#include <lumex/secure/common.hpp>
#include <lumex/secure/network_params.hpp>

#include <boost/io/ios_state.hpp>
#include <boost/multiprecision/cpp_int.hpp>

#include <algorithm>
#include <ostream>
#include <stdexcept>

#include <crypto/ed25519-donna/ed25519.h>
#include <cryptopp/aes.h>
#include <cryptopp/modes.h>

/*
 * Constants
 */

lumex::uint128_t const lumex::Klumex_ratio = lumex::uint128_t ("1000000000000000000000000000000000"); // 10^33 = 1000 lumex
lumex::uint128_t const lumex::lumex_ratio = lumex::uint128_t ("1000000000000000000000000000000"); // 10^30 = 1 lumex
lumex::uint128_t const lumex::raw_ratio = lumex::uint128_t ("1"); // 10^0

/*
 * uint128_union
 */

lumex::uint128_union::uint128_union (uint64_t value) :
	uint128_union (lumex::uint128_t{ value })
{
}

lumex::uint128_union::uint128_union (lumex::uint128_t const & value)
{
	bytes.fill (0);
	boost::multiprecision::export_bits (value, bytes.rbegin (), 8, false);
}

lumex::uint128_t lumex::uint128_union::number () const
{
	lumex::uint128_t result;
	boost::multiprecision::import_bits (result, bytes.begin (), bytes.end ());
	return result;
}

lumex::uint128_union::operator lumex::uint128_t () const
{
	return number ();
}

/*
 * amount
 */

lumex::amount::operator lumex::uint128_t () const
{
	return number ();
}

/*
 * uint256_union
 */

lumex::uint256_union::uint256_union (uint64_t value) :
	uint256_union (lumex::uint256_t{ value })
{
}

lumex::uint256_union::uint256_union (lumex::uint256_t const & value)
{
	bytes.fill (0);
	boost::multiprecision::export_bits (value, bytes.rbegin (), 8, false);
}

lumex::uint256_t lumex::uint256_union::number () const
{
	lumex::uint256_t result;
	boost::multiprecision::import_bits (result, bytes.begin (), bytes.end ());
	return result;
}

lumex::uint256_union::operator lumex::uint256_t () const
{
	return number ();
}

/*
 * block_hash
 */

lumex::block_hash::operator lumex::uint256_t () const
{
	return number ();
}

/*
 * public_key
 */

lumex::public_key::operator lumex::uint256_t () const
{
	return number ();
}

/*
 * hash_or_account
 */

lumex::hash_or_account::operator lumex::uint256_t () const
{
	return raw.number ();
}

/*
 * uint512_union
 */

lumex::uint512_union::uint512_union (uint64_t value) :
	uint512_union (lumex::uint512_t{ value })
{
}

lumex::uint512_union::uint512_union (lumex::uint512_t const & value)
{
	bytes.fill (0);
	boost::multiprecision::export_bits (value, bytes.rbegin (), 8, false);
}

lumex::uint512_t lumex::uint512_union::number () const
{
	lumex::uint512_t result;
	boost::multiprecision::import_bits (result, bytes.begin (), bytes.end ());
	return result;
}

lumex::uint512_union::operator lumex::uint512_t () const
{
	return number ();
}

namespace
{
char const * account_lookup ("13456789abcdefghijkmnopqrstuwxyz");
char const * account_reverse ("~0~1234567~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~89:;<=>?@AB~CDEFGHIJK~LMNO~~~~~");
char account_encode (uint8_t value)
{
	debug_assert (value < 32);
	auto result (account_lookup[value]);
	return result;
}
uint8_t account_decode (char value)
{
	debug_assert (value >= '0');
	debug_assert (value <= '~');
	auto result (account_reverse[value - 0x30]);
	if (result != '~')
	{
		result -= 0x30;
	}
	return result;
}
}

/*
 * public_key
 */

lumex::public_key lumex::public_key::from_account (std::string const & text)
{
	lumex::public_key result;
	bool error = result.decode_account (text);
	release_assert (!error);
	return result;
}

lumex::public_key lumex::public_key::from_node_id (std::string const & text)
{
	lumex::public_key result;
	bool error = result.decode_node_id (text);
	release_assert (!error);
	return result;
}

void lumex::public_key::encode_account (std::ostream & os) const
{
	uint64_t check = 0;

	blake2b_state hash;
	blake2b_init (&hash, 5);
	blake2b_update (&hash, bytes.data (), bytes.size ());
	blake2b_final (&hash, reinterpret_cast<uint8_t *> (&check), 5);

	lumex::uint512_t number_l{ number () };
	number_l <<= 40;
	number_l |= lumex::uint512_t{ check };

	// Pre-calculate all characters in reverse order
	std::array<char, 60> encoded{};
	for (auto i (0); i < 60; ++i)
	{
		uint8_t r{ number_l & static_cast<uint8_t> (0x1f) };
		number_l >>= 5;
		encoded[59 - i] = account_encode (r);
	}

	// Write prefix
	os << "lumex_";

	// Write encoded characters
	os.write (encoded.data (), encoded.size ());
}

std::string lumex::public_key::to_account () const
{
	std::stringstream stream;
	encode_account (stream);
	return stream.str ();
}

lumex::public_key const & lumex::public_key::null ()
{
	return lumex::hardened_constants::get ().not_an_account;
}

std::string lumex::public_key::to_node_id () const
{
	std::stringstream stream;
	encode_node_id (stream);
	return stream.str ();
}

void lumex::public_key::encode_node_id (std::ostream & os) const
{
	// Same encoding as account but with "node_" prefix instead of "lumex_"
	uint64_t check = 0;

	blake2b_state hash;
	blake2b_init (&hash, 5);
	blake2b_update (&hash, bytes.data (), bytes.size ());
	blake2b_final (&hash, reinterpret_cast<uint8_t *> (&check), 5);

	lumex::uint512_t number_l{ number () };
	number_l <<= 40;
	number_l |= lumex::uint512_t{ check };

	std::array<char, 60> encoded{};
	for (auto i (0); i < 60; ++i)
	{
		uint8_t r{ number_l & static_cast<uint8_t> (0x1f) };
		number_l >>= 5;
		encoded[59 - i] = account_encode (r);
	}

	os << "node_";
	os.write (encoded.data (), encoded.size ());
}

bool lumex::public_key::decode_node_id (std::string const & source_a)
{
	return decode_account (source_a);
}

bool lumex::public_key::decode_account (std::string const & source_a)
{
	auto error (source_a.size () < 5);
	if (!error)
	{
		auto xrb_prefix (source_a[0] == 'x' && source_a[1] == 'r' && source_a[2] == 'b' && (source_a[3] == '_' || source_a[3] == '-'));
		auto nano_prefix (source_a[0] == 'n' && source_a[1] == 'a' && source_a[2] == 'n' && source_a[3] == 'o' && (source_a[4] == '_' || source_a[4] == '-'));
		auto lumex_prefix (source_a.size () >= 6 && source_a[0] == 'l' && source_a[1] == 'u' && source_a[2] == 'm' && source_a[3] == 'e' && source_a[4] == 'x' && (source_a[5] == '_' || source_a[5] == '-'));
		auto node_id_prefix = (source_a[0] == 'n' && source_a[1] == 'o' && source_a[2] == 'd' && source_a[3] == 'e' && source_a[4] == '_');
		error = (xrb_prefix && source_a.size () != 64) || (nano_prefix && source_a.size () != 65) || (lumex_prefix && source_a.size () != 66) || (node_id_prefix && source_a.size () != 65);
		if (!error)
		{
			if (xrb_prefix || nano_prefix || lumex_prefix || node_id_prefix)
			{
				size_t offset = 5;
				if (xrb_prefix)
				{
					offset = 4;
				}
				else if (lumex_prefix)
				{
					offset = 6;
				}
				auto i (source_a.begin () + offset);
				if (*i == '1' || *i == '3')
				{
					lumex::uint512_t number_l;
					for (auto j (source_a.end ()); !error && i != j; ++i)
					{
						uint8_t character (*i);
						error = character < 0x30 || character >= 0x80;
						if (!error)
						{
							uint8_t byte (account_decode (character));
							error = byte == '~';
							if (!error)
							{
								number_l <<= 5;
								number_l += byte;
							}
						}
					}
					if (!error)
					{
						lumex::public_key temp = (number_l >> 40).convert_to<lumex::uint256_t> ();
						uint64_t check (number_l & static_cast<uint64_t> (0xffffffffff));
						uint64_t validation (0);
						blake2b_state hash;
						blake2b_init (&hash, 5);
						blake2b_update (&hash, temp.bytes.data (), temp.bytes.size ());
						blake2b_final (&hash, reinterpret_cast<uint8_t *> (&validation), 5);
						error = check != validation;
						if (!error)
						{
							*this = temp;
						}
					}
				}
				else
				{
					error = true;
				}
			}
			else
			{
				error = decode_hex (source_a);
			}
		}
	}
	return error;
}

/*
 * uint256_union
 */

// Construct a uint256_union = AES_ENC_CTR (cleartext, key, iv)
void lumex::uint256_union::encrypt (lumex::raw_key const & cleartext, lumex::raw_key const & key, uint128_union const & iv)
{
	CryptoPP::AES::Encryption alg (key.bytes.data (), sizeof (key.bytes));
	CryptoPP::CTR_Mode_ExternalCipher::Encryption enc (alg, iv.bytes.data ());
	enc.ProcessData (bytes.data (), cleartext.bytes.data (), sizeof (cleartext.bytes));
}

lumex::uint256_union & lumex::uint256_union::operator^= (lumex::uint256_union const & other_a)
{
	auto j (other_a.qwords.begin ());
	for (auto i (qwords.begin ()), n (qwords.end ()); i != n; ++i, ++j)
	{
		*i ^= *j;
	}
	return *this;
}

lumex::uint256_union lumex::uint256_union::operator^ (lumex::uint256_union const & other_a) const
{
	lumex::uint256_union result;
	auto k (result.qwords.begin ());
	for (auto i (qwords.begin ()), j (other_a.qwords.begin ()), n (qwords.end ()); i != n; ++i, ++j, ++k)
	{
		*k = *i ^ *j;
	}
	return result;
}

lumex::uint256_union::uint256_union (std::string const & hex_a)
{
	auto error (decode_hex (hex_a));
	release_assert (!error);
}

void lumex::uint256_union::encode_hex (std::ostream & stream) const
{
	boost::io::ios_flags_saver ifs{ stream };
	stream << std::hex << std::uppercase << std::noshowbase << std::setw (64) << std::setfill ('0');
	stream << number ();
}

bool lumex::uint256_union::decode_hex (std::string const & text)
{
	auto error (false);
	if (!text.empty () && text.size () <= 64)
	{
		std::stringstream stream (text);
		stream << std::hex << std::noshowbase;
		lumex::uint256_t number_l;
		try
		{
			stream >> number_l;
			*this = number_l;
			if (!stream.eof ())
			{
				error = true;
			}
		}
		catch (std::runtime_error &)
		{
			error = true;
		}
	}
	else
	{
		error = true;
	}
	return error;
}

void lumex::uint256_union::encode_dec (std::ostream & stream) const
{
	boost::io::ios_flags_saver ifs{ stream };
	stream << std::dec << std::noshowbase;
	stream << number ();
}

bool lumex::uint256_union::decode_dec (std::string const & text)
{
	auto error (text.size () > 78 || (text.size () > 1 && text.front () == '0') || (!text.empty () && text.front () == '-'));
	if (!error)
	{
		std::stringstream stream (text);
		stream << std::dec << std::noshowbase;
		lumex::uint256_t number_l;
		try
		{
			stream >> number_l;
			*this = number_l;
			if (!stream.eof ())
			{
				error = true;
			}
		}
		catch (std::runtime_error &)
		{
			error = true;
		}
	}
	return error;
}

std::string lumex::uint256_union::to_string () const
{
	std::stringstream stream;
	encode_hex (stream);
	return stream.str ();
}

std::string lumex::uint256_union::to_string_dec () const
{
	std::stringstream stream;
	encode_dec (stream);
	return stream.str ();
}

/*
 * uint512_union
 */

void lumex::uint512_union::encode_hex (std::ostream & stream) const
{
	boost::io::ios_flags_saver ifs{ stream };
	stream << std::hex << std::uppercase << std::noshowbase << std::setw (128) << std::setfill ('0');
	stream << number ();
}

bool lumex::uint512_union::decode_hex (std::string const & text)
{
	auto error (text.size () > 128);
	if (!error)
	{
		std::stringstream stream (text);
		stream << std::hex << std::noshowbase;
		lumex::uint512_t number_l;
		try
		{
			stream >> number_l;
			*this = number_l;
			if (!stream.eof ())
			{
				error = true;
			}
		}
		catch (std::runtime_error &)
		{
			error = true;
		}
	}
	return error;
}

std::string lumex::uint512_union::to_string () const
{
	std::stringstream stream;
	encode_hex (stream);
	return stream.str ();
}

/*
 * raw_key
 */

lumex::raw_key::~raw_key ()
{
	secure_wipe_memory (bytes.data (), bytes.size ());
}

// This this = AES_DEC_CTR (ciphertext, key, iv)
void lumex::raw_key::decrypt (lumex::uint256_union const & ciphertext, lumex::raw_key const & key_a, uint128_union const & iv)
{
	CryptoPP::AES::Encryption alg (key_a.bytes.data (), sizeof (key_a.bytes));
	CryptoPP::CTR_Mode_ExternalCipher::Decryption dec (alg, iv.bytes.data ());
	dec.ProcessData (bytes.data (), ciphertext.bytes.data (), sizeof (ciphertext.bytes));
}

lumex::raw_key lumex::deterministic_key (lumex::raw_key const & seed_a, uint32_t index_a)
{
	lumex::raw_key prv_key;
	blake2b_state hash;
	blake2b_init (&hash, prv_key.bytes.size ());
	blake2b_update (&hash, seed_a.bytes.data (), seed_a.bytes.size ());
	lumex::uint256_union index (index_a);
	blake2b_update (&hash, reinterpret_cast<uint8_t *> (&index.dwords[7]), sizeof (uint32_t));
	blake2b_final (&hash, prv_key.bytes.data (), prv_key.bytes.size ());
	return prv_key;
}

lumex::public_key lumex::pub_key (lumex::raw_key const & raw_key_a)
{
	lumex::public_key result;
	ed25519_publickey (raw_key_a.bytes.data (), result.bytes.data ());
	return result;
}

lumex::signature lumex::sign_message (lumex::raw_key const & private_key, lumex::public_key const & public_key, uint8_t const * data, size_t size)
{
	lumex::signature result;
	ed25519_sign (data, size, private_key.bytes.data (), public_key.bytes.data (), result.bytes.data ());
	return result;
}

lumex::signature lumex::sign_message (lumex::raw_key const & private_key, lumex::public_key const & public_key, lumex::uint256_union const & message)
{
	return lumex::sign_message (private_key, public_key, message.bytes.data (), sizeof (message.bytes));
}

bool lumex::validate_message (lumex::public_key const & public_key, uint8_t const * data, size_t size, lumex::signature const & signature)
{
	return 0 != ed25519_sign_open (data, size, public_key.bytes.data (), signature.bytes.data ());
}

bool lumex::validate_message (lumex::public_key const & public_key, lumex::uint256_union const & message, lumex::signature const & signature)
{
	return validate_message (public_key, message.bytes.data (), sizeof (message.bytes), signature);
}

/*
 * uint128_union
 */

lumex::uint128_union::uint128_union (std::string const & string_a)
{
	auto error (decode_hex (string_a));
	release_assert (!error);
}

void lumex::uint128_union::encode_hex (std::ostream & stream) const
{
	boost::io::ios_flags_saver ifs{ stream };
	stream << std::hex << std::uppercase << std::noshowbase << std::setw (32) << std::setfill ('0');
	stream << number ();
}

bool lumex::uint128_union::decode_hex (std::string const & text)
{
	auto error (text.size () > 32);
	if (!error)
	{
		std::stringstream stream (text);
		stream << std::hex << std::noshowbase;
		lumex::uint128_t number_l;
		try
		{
			stream >> number_l;
			*this = number_l;
			if (!stream.eof ())
			{
				error = true;
			}
		}
		catch (std::runtime_error &)
		{
			error = true;
		}
	}
	return error;
}

void lumex::uint128_union::encode_dec (std::ostream & stream) const
{
	boost::io::ios_flags_saver ifs{ stream };
	stream << std::dec << std::noshowbase;
	stream << number ();
}

bool lumex::uint128_union::decode_dec (std::string const & text, bool decimal)
{
	auto error (text.size () > 39 || (text.size () > 1 && text.front () == '0' && !decimal) || (!text.empty () && text.front () == '-'));
	if (!error)
	{
		std::stringstream stream (text);
		stream << std::dec << std::noshowbase;
		boost::multiprecision::checked_uint128_t number_l;
		try
		{
			stream >> number_l;
			lumex::uint128_t unchecked (number_l);
			*this = unchecked;
			if (!stream.eof ())
			{
				error = true;
			}
		}
		catch (std::runtime_error &)
		{
			error = true;
		}
	}
	return error;
}

bool lumex::uint128_union::decode_dec (std::string const & text, lumex::uint128_t const & scale)
{
	bool error (text.size () > 40 || (!text.empty () && text.front () == '-'));
	if (!error)
	{
		auto delimiter_position (text.find (".")); // Dot delimiter hardcoded until decision for supporting other locales
		if (delimiter_position == std::string::npos)
		{
			lumex::uint128_union integer;
			error = integer.decode_dec (text);
			if (!error)
			{
				// Overflow check
				try
				{
					auto result (boost::multiprecision::checked_uint128_t (integer.number ()) * boost::multiprecision::checked_uint128_t (scale));
					error = (result > std::numeric_limits<lumex::uint128_t>::max ());
					if (!error)
					{
						*this = lumex::uint128_t (result);
					}
				}
				catch (std::overflow_error &)
				{
					error = true;
				}
			}
		}
		else
		{
			lumex::uint128_union integer_part;
			std::string integer_text (text.substr (0, delimiter_position));
			error = (integer_text.empty () || integer_part.decode_dec (integer_text));
			if (!error)
			{
				// Overflow check
				try
				{
					error = ((boost::multiprecision::checked_uint128_t (integer_part.number ()) * boost::multiprecision::checked_uint128_t (scale)) > std::numeric_limits<lumex::uint128_t>::max ());
				}
				catch (std::overflow_error &)
				{
					error = true;
				}
				if (!error)
				{
					lumex::uint128_union decimal_part;
					std::string decimal_text (text.substr (delimiter_position + 1, text.length ()));
					error = (decimal_text.empty () || decimal_part.decode_dec (decimal_text, true));
					if (!error)
					{
						// Overflow check
						auto scale_length (scale.convert_to<std::string> ().length ());
						error = (scale_length <= decimal_text.length ());
						if (!error)
						{
							auto base10 = boost::multiprecision::cpp_int (10);
							release_assert ((scale_length - decimal_text.length () - 1) <= std::numeric_limits<unsigned>::max ());
							auto pow10 = boost::multiprecision::pow (base10, static_cast<unsigned> (scale_length - decimal_text.length () - 1));
							auto decimal_part_num = decimal_part.number ();
							auto integer_part_scaled = integer_part.number () * scale;
							auto decimal_part_mult_pow = decimal_part_num * pow10;
							auto result = integer_part_scaled + decimal_part_mult_pow;

							// Overflow check
							error = (result > std::numeric_limits<lumex::uint128_t>::max ());
							if (!error)
							{
								*this = lumex::uint128_t (result);
							}
						}
					}
				}
			}
		}
	}
	return error;
}

std::string lumex::uint128_union::to_string () const
{
	std::stringstream stream;
	encode_hex (stream);
	return stream.str ();
}

std::string lumex::uint128_union::to_string_dec () const
{
	std::stringstream stream;
	encode_dec (stream);
	return stream.str ();
}

void lumex::uint128_union::encode_balance (std::ostream & os, lumex::uint128_t const & scale, int precision, bool group_digits) const
{
	lumex::encode_balance (os, number (), scale, precision, group_digits);
}

std::string lumex::uint128_union::format_balance (lumex::uint128_t const & scale, int precision, bool group_digits) const
{
	std::ostringstream stream;
	encode_balance (stream, scale, precision, group_digits);
	return stream.str ();
}

/*
 * hash_or_account
 */

bool lumex::hash_or_account::decode_hex (std::string const & text_a)
{
	return raw.decode_hex (text_a);
}

bool lumex::hash_or_account::decode_account (std::string const & source_a)
{
	return account.decode_account (source_a);
}

std::string lumex::hash_or_account::to_string () const
{
	return raw.to_string ();
}

std::string lumex::hash_or_account::to_account () const
{
	return account.to_account ();
}

/*
 * link
 */

lumex::link::link (std::string_view str)
{
	release_assert (str.size () <= bytes.size ());
	std::copy_n (str.data (), str.size (), bytes.begin ());
}

/*
 *
 */

void lumex::encode_balance (std::ostream & os, lumex::uint128_t const & value, lumex::uint128_t const & scale, int precision, bool group_digits)
{
	auto thousands_sep = std::use_facet<std::numpunct<char>> (std::locale ()).thousands_sep ();
	auto decimal_point = std::use_facet<std::numpunct<char>> (std::locale ()).decimal_point ();
	std::string grouping = "\3";
	lumex::encode_balance (os, value, scale, precision, group_digits, thousands_sep, decimal_point, grouping);
}

std::string lumex::to_string_hex (uint64_t const value_a)
{
	std::stringstream stream;
	stream << std::hex << std::noshowbase << std::setw (16) << std::setfill ('0');
	stream << value_a;
	return stream.str ();
}

std::string lumex::to_string_hex (uint16_t const value_a)
{
	std::stringstream stream;
	stream << std::hex << std::noshowbase << std::setw (4) << std::setfill ('0');
	stream << value_a;
	return stream.str ();
}

bool lumex::from_string_hex (std::string const & value_a, uint64_t & target_a)
{
	auto error (value_a.empty ());
	if (!error)
	{
		error = value_a.size () > 16;
		if (!error)
		{
			std::stringstream stream (value_a);
			stream << std::hex << std::noshowbase;
			try
			{
				uint64_t number_l;
				stream >> number_l;
				target_a = number_l;
				if (!stream.eof ())
				{
					error = true;
				}
			}
			catch (std::runtime_error &)
			{
				error = true;
			}
		}
	}
	return error;
}

std::string lumex::to_string (double const value_a, int const precision_a)
{
	std::stringstream stream;
	stream << std::setprecision (precision_a) << std::fixed;
	stream << value_a;
	return stream.str ();
}

std::ostream & lumex::operator<< (std::ostream & os, const lumex::uint128_union & val)
{
	val.encode_hex (os);
	return os;
}

std::ostream & lumex::operator<< (std::ostream & os, const lumex::uint256_union & val)
{
	val.encode_hex (os);
	return os;
}

std::ostream & lumex::operator<< (std::ostream & os, const lumex::uint512_union & val)
{
	val.encode_hex (os);
	return os;
}

std::ostream & lumex::operator<< (std::ostream & os, const lumex::hash_or_account & val)
{
	val.raw.encode_hex (os);
	return os;
}

std::ostream & lumex::operator<< (std::ostream & os, const lumex::account & val)
{
	val.encode_account (os);
	return os;
}

/*
 * wallet_id parsing
 */

std::optional<lumex::wallet_id> lumex::try_parse_wallet_id (std::string const & text)
{
	lumex::wallet_id id;
	if (id.decode_hex (text))
	{
		return std::nullopt;
	}
	return id;
}

lumex::wallet_id lumex::parse_wallet_id (std::string const & text)
{
	if (auto id = try_parse_wallet_id (text))
	{
		return *id;
	}
	throw std::invalid_argument{ "Invalid wallet id: " + text };
}

/*
 *
 */

#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable : 4146) // warning C4146: unary minus operator applied to unsigned type, result still unsigned
#endif

uint64_t lumex::difficulty::from_multiplier (double const multiplier_a, uint64_t const base_difficulty_a)
{
	if (multiplier_a <= 0.)
	{
		return 0;
	}
	lumex::uint128_t reverse_difficulty ((-base_difficulty_a) / multiplier_a);
	if (reverse_difficulty > std::numeric_limits<std::uint64_t>::max ())
	{
		return 0;
	}
	else if (reverse_difficulty != 0 || base_difficulty_a == 0 || multiplier_a < 1.)
	{
		return -(static_cast<uint64_t> (reverse_difficulty));
	}
	else
	{
		return std::numeric_limits<std::uint64_t>::max ();
	}
}

double lumex::difficulty::to_multiplier (uint64_t const difficulty_a, uint64_t const base_difficulty_a)
{
	if (difficulty_a == 0)
	{
		return 0;
	}
	return static_cast<double> (-base_difficulty_a) / (-difficulty_a);
}

#ifdef _WIN32
#pragma warning(pop)
#endif
