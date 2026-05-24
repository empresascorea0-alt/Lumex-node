#pragma once

#include <lumex/lib/block_sideband.hpp>
#include <lumex/lib/fwd.hpp>
#include <lumex/lib/optional_ptr.hpp>

#include <boost/property_tree/ptree_fwd.hpp>

#include <array>
#include <optional>

typedef struct blake2b_state__ blake2b_state;

namespace lumex
{
using block_uniquer = lumex::uniquer<lumex::uint256_union, lumex::block>;
}

namespace lumex
{
class block
{
public:
	virtual ~block () = default;

	// Return a digest of the hashables in this block.
	lumex::block_hash const & hash () const;
	// Return a digest of hashables and non-hashables in this block.
	lumex::block_hash full_hash () const;
	lumex::block_sideband const & sideband () const;
	void sideband_set (lumex::block_sideband const &);
	bool has_sideband () const;
	std::string to_json () const;
	virtual uint64_t block_work () const = 0;
	virtual void block_work_set (uint64_t) = 0;
	// Previous block or account number for open blocks
	virtual lumex::root root () const = 0;
	// Qualified root value based on previous() and root()
	virtual lumex::qualified_root qualified_root () const;
	virtual void serialize (lumex::stream &) const = 0;
	virtual void serialize_json (std::string &, bool = false) const = 0;
	virtual void serialize_json (boost::property_tree::ptree &) const = 0;
	virtual void visit (lumex::block_visitor &) const = 0;
	virtual void visit (lumex::mutable_block_visitor &) = 0;
	virtual bool operator== (lumex::block const &) const = 0;
	virtual lumex::block_type type () const = 0;
	virtual lumex::signature const & block_signature () const = 0;
	virtual void signature_set (lumex::signature const &) = 0;
	virtual bool valid_predecessor (lumex::block const &) const = 0;
	virtual lumex::work_version work_version () const;
	virtual std::shared_ptr<lumex::block> clone () const = 0;

	// If there are any changes to the hashables, call this to update the cached hash
	void refresh ();

	bool is_send () const noexcept;
	bool is_receive () const noexcept;
	bool is_change () const noexcept;
	bool is_epoch () const noexcept;

	// Returns block hashes that this block depends on (must be confirmed before this block)
	// Non-zero entries are: [0] = previous block, [1] = source/send block (for receives)
	std::array<lumex::block_hash, 2> dependencies () const;

	static size_t size (lumex::block_type);

public: // Direct access to the block fields or nullopt if the block type does not have the specified field
	// Returns account field or account from sideband
	lumex::account account () const noexcept;
	// Account field for open/state blocks
	virtual std::optional<lumex::account> account_field () const;
	// Returns the balance field or balance from sideband
	lumex::amount balance () const noexcept;
	// Balance field for open/send/state blocks
	virtual std::optional<lumex::amount> balance_field () const;
	// Returns the destination account for send/state blocks that are sends
	lumex::account destination () const noexcept;
	// Destination account for send blocks
	virtual std::optional<lumex::account> destination_field () const;
	// Link field for state blocks
	virtual std::optional<lumex::link> link_field () const;
	// Previous block if field exists or 0
	lumex::block_hash previous () const noexcept;
	// Previous block in chain if the field exists
	virtual std::optional<lumex::block_hash> previous_field () const = 0;
	// Representative field for open/change blocks
	virtual std::optional<lumex::account> representative_field () const;
	// Returns the source block hash for open/receive/state blocks that are receives
	lumex::block_hash source () const noexcept;
	// Source block for open/receive blocks
	virtual std::optional<lumex::block_hash> source_field () const;

protected:
	virtual void generate_hash (blake2b_state &) const = 0;
	mutable lumex::block_hash cached_hash{ 0 };

	/**
	 * Contextual details about a block, some fields may or may not be set depending on block type.
	 * This field is set via sideband_set in ledger processing or deserializing blocks from the database.
	 * Otherwise it may be null (for example, an old block or fork).
	 */
	lumex::optional_ptr<lumex::block_sideband> sideband_m;

private:
	lumex::block_hash generate_hash () const;

public: // Logging
	virtual void operator() (lumex::object_stream &) const;
};

class send_hashables
{
public:
	send_hashables () = default;
	send_hashables (lumex::block_hash const &, lumex::account const &, lumex::amount const &);
	send_hashables (bool &, lumex::stream &);
	send_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	lumex::block_hash previous;
	lumex::account destination;
	lumex::amount balance;
	static std::size_t constexpr size = sizeof (previous) + sizeof (destination) + sizeof (balance);
};

class send_block : public lumex::block
{
public:
	send_block () = default;
	send_block (lumex::block_hash const &, lumex::account const &, lumex::amount const &, lumex::raw_key const &, lumex::public_key const &, uint64_t);
	send_block (bool &, lumex::stream &);
	send_block (bool &, boost::property_tree::ptree const &);
	virtual ~send_block () = default;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	lumex::root root () const override;
	void serialize (lumex::stream &) const override;
	bool deserialize (lumex::stream &);
	void serialize_json (std::string &, bool = false) const override;
	void serialize_json (boost::property_tree::ptree &) const override;
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (lumex::block_visitor &) const override;
	void visit (lumex::mutable_block_visitor &) override;
	lumex::block_type type () const override;
	lumex::signature const & block_signature () const override;
	void signature_set (lumex::signature const &) override;
	bool operator== (lumex::block const &) const override;
	bool operator== (lumex::send_block const &) const;
	bool valid_predecessor (lumex::block const &) const override;
	std::shared_ptr<lumex::block> clone () const override;
	send_hashables hashables;
	lumex::signature signature;
	uint64_t work;
	static std::size_t constexpr size = lumex::send_hashables::size + sizeof (signature) + sizeof (work);

public: // Send block fields
	std::optional<lumex::amount> balance_field () const override;
	std::optional<lumex::account> destination_field () const override;
	std::optional<lumex::block_hash> previous_field () const override;

public: // Logging
	void operator() (lumex::object_stream &) const override;

protected:
	void generate_hash (blake2b_state &) const override;
};

class receive_hashables
{
public:
	receive_hashables () = default;
	receive_hashables (lumex::block_hash const &, lumex::block_hash const &);
	receive_hashables (bool &, lumex::stream &);
	receive_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	lumex::block_hash previous;
	lumex::block_hash source;
	static std::size_t constexpr size = sizeof (previous) + sizeof (source);
};

class receive_block : public lumex::block
{
public:
	receive_block () = default;
	receive_block (lumex::block_hash const &, lumex::block_hash const &, lumex::raw_key const &, lumex::public_key const &, uint64_t);
	receive_block (bool &, lumex::stream &);
	receive_block (bool &, boost::property_tree::ptree const &);
	virtual ~receive_block () = default;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	lumex::root root () const override;
	void serialize (lumex::stream &) const override;
	bool deserialize (lumex::stream &);
	void serialize_json (std::string &, bool = false) const override;
	void serialize_json (boost::property_tree::ptree &) const override;
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (lumex::block_visitor &) const override;
	void visit (lumex::mutable_block_visitor &) override;
	lumex::block_type type () const override;
	lumex::signature const & block_signature () const override;
	void signature_set (lumex::signature const &) override;
	bool operator== (lumex::block const &) const override;
	bool operator== (lumex::receive_block const &) const;
	bool valid_predecessor (lumex::block const &) const override;
	std::shared_ptr<lumex::block> clone () const override;
	receive_hashables hashables;
	lumex::signature signature;
	uint64_t work;
	static std::size_t constexpr size = lumex::receive_hashables::size + sizeof (signature) + sizeof (work);

public: // Receive block fields
	std::optional<lumex::block_hash> previous_field () const override;
	std::optional<lumex::block_hash> source_field () const override;

public: // Logging
	void operator() (lumex::object_stream &) const override;

protected:
	void generate_hash (blake2b_state &) const override;
};

class open_hashables
{
public:
	open_hashables () = default;
	open_hashables (lumex::block_hash const &, lumex::account const &, lumex::account const &);
	open_hashables (bool &, lumex::stream &);
	open_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	lumex::block_hash source;
	lumex::account representative;
	lumex::account account;
	static std::size_t constexpr size = sizeof (source) + sizeof (representative) + sizeof (account);
};

class open_block : public lumex::block
{
public:
	open_block () = default;
	open_block (lumex::block_hash const &, lumex::account const &, lumex::account const &, lumex::raw_key const &, lumex::public_key const &, uint64_t);
	open_block (lumex::block_hash const &, lumex::account const &, lumex::account const &, std::nullptr_t);
	open_block (bool &, lumex::stream &);
	open_block (bool &, boost::property_tree::ptree const &);
	virtual ~open_block () = default;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	lumex::root root () const override;
	void serialize (lumex::stream &) const override;
	bool deserialize (lumex::stream &);
	void serialize_json (std::string &, bool = false) const override;
	void serialize_json (boost::property_tree::ptree &) const override;
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (lumex::block_visitor &) const override;
	void visit (lumex::mutable_block_visitor &) override;
	lumex::block_type type () const override;
	lumex::signature const & block_signature () const override;
	void signature_set (lumex::signature const &) override;
	bool operator== (lumex::block const &) const override;
	bool operator== (lumex::open_block const &) const;
	bool valid_predecessor (lumex::block const &) const override;
	std::shared_ptr<lumex::block> clone () const override;
	lumex::open_hashables hashables;
	lumex::signature signature;
	uint64_t work;
	static std::size_t constexpr size = lumex::open_hashables::size + sizeof (signature) + sizeof (work);

public: // Open block fields
	std::optional<lumex::account> account_field () const override;
	std::optional<lumex::block_hash> previous_field () const override;
	std::optional<lumex::account> representative_field () const override;
	std::optional<lumex::block_hash> source_field () const override;

public: // Logging
	void operator() (lumex::object_stream &) const override;

protected:
	void generate_hash (blake2b_state &) const override;
};

class change_hashables
{
public:
	change_hashables () = default;
	change_hashables (lumex::block_hash const &, lumex::account const &);
	change_hashables (bool &, lumex::stream &);
	change_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	lumex::block_hash previous;
	lumex::account representative;
	static std::size_t constexpr size = sizeof (previous) + sizeof (representative);
};

class change_block : public lumex::block
{
public:
	change_block () = default;
	change_block (lumex::block_hash const &, lumex::account const &, lumex::raw_key const &, lumex::public_key const &, uint64_t);
	change_block (bool &, lumex::stream &);
	change_block (bool &, boost::property_tree::ptree const &);
	virtual ~change_block () = default;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	lumex::root root () const override;
	void serialize (lumex::stream &) const override;
	bool deserialize (lumex::stream &);
	void serialize_json (std::string &, bool = false) const override;
	void serialize_json (boost::property_tree::ptree &) const override;
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (lumex::block_visitor &) const override;
	void visit (lumex::mutable_block_visitor &) override;
	lumex::block_type type () const override;
	lumex::signature const & block_signature () const override;
	void signature_set (lumex::signature const &) override;
	bool operator== (lumex::block const &) const override;
	bool operator== (lumex::change_block const &) const;
	bool valid_predecessor (lumex::block const &) const override;
	std::shared_ptr<lumex::block> clone () const override;
	lumex::change_hashables hashables;
	lumex::signature signature;
	uint64_t work;
	static std::size_t constexpr size = lumex::change_hashables::size + sizeof (signature) + sizeof (work);

public: // Change block fields
	std::optional<lumex::block_hash> previous_field () const override;
	std::optional<lumex::account> representative_field () const override;

public: // Logging
	void operator() (lumex::object_stream &) const override;

protected:
	void generate_hash (blake2b_state &) const override;
};

class state_hashables
{
public:
	state_hashables () = default;
	state_hashables (lumex::account const &, lumex::block_hash const &, lumex::account const &, lumex::amount const &, lumex::link const &);
	state_hashables (bool &, lumex::stream &);
	state_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	// Account# / public key that operates this account
	// Uses:
	// Bulk signature validation in advance of further ledger processing
	// Arranging uncomitted transactions by account
	lumex::account account;
	// Previous transaction in this chain
	lumex::block_hash previous;
	// Representative of this account
	lumex::account representative;
	// Current balance of this account
	// Allows lookup of account balance simply by looking at the head block
	lumex::amount balance;
	// Link field contains source block_hash if receiving, destination account if sending
	lumex::link link;
	// Serialized size
	static std::size_t constexpr size = sizeof (account) + sizeof (previous) + sizeof (representative) + sizeof (balance) + sizeof (link);
};

class state_block : public lumex::block
{
public:
	state_block () = default;
	state_block (lumex::account const & account,
	lumex::block_hash const & previous,
	lumex::account const & representative,
	lumex::amount const & balance,
	lumex::link const & link,
	lumex::raw_key const & prv,
	lumex::public_key const & pub,
	uint64_t work);
	state_block (bool &, lumex::stream &);
	state_block (bool &, boost::property_tree::ptree const &);
	virtual ~state_block () = default;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	lumex::root root () const override;
	void serialize (lumex::stream &) const override;
	bool deserialize (lumex::stream &);
	void serialize_json (std::string &, bool = false) const override;
	void serialize_json (boost::property_tree::ptree &) const override;
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (lumex::block_visitor &) const override;
	void visit (lumex::mutable_block_visitor &) override;
	lumex::block_type type () const override;
	lumex::signature const & block_signature () const override;
	void signature_set (lumex::signature const &) override;
	bool operator== (lumex::block const &) const override;
	bool operator== (lumex::state_block const &) const;
	bool valid_predecessor (lumex::block const &) const override;
	std::shared_ptr<lumex::block> clone () const override;
	lumex::state_hashables hashables;
	lumex::signature signature;
	uint64_t work;
	static std::size_t constexpr size = lumex::state_hashables::size + sizeof (signature) + sizeof (work);

public: // State block fields
	std::optional<lumex::account> account_field () const override;
	std::optional<lumex::amount> balance_field () const override;
	std::optional<lumex::link> link_field () const override;
	std::optional<lumex::block_hash> previous_field () const override;
	std::optional<lumex::account> representative_field () const override;

public: // Logging
	void operator() (lumex::object_stream &) const override;

protected:
	void generate_hash (blake2b_state &) const override;
};

class block_visitor
{
public:
	virtual void send_block (lumex::send_block const &) = 0;
	virtual void receive_block (lumex::receive_block const &) = 0;
	virtual void open_block (lumex::open_block const &) = 0;
	virtual void change_block (lumex::change_block const &) = 0;
	virtual void state_block (lumex::state_block const &) = 0;
	virtual ~block_visitor () = default;
};
class mutable_block_visitor
{
public:
	virtual void send_block (lumex::send_block &) = 0;
	virtual void receive_block (lumex::receive_block &) = 0;
	virtual void open_block (lumex::open_block &) = 0;
	virtual void change_block (lumex::change_block &) = 0;
	virtual void state_block (lumex::state_block &) = 0;
	virtual ~mutable_block_visitor () = default;
};

std::shared_ptr<lumex::block> deserialize_block (lumex::stream &);
std::shared_ptr<lumex::block> deserialize_block (lumex::stream &, lumex::block_type, lumex::block_uniquer * = nullptr);
std::shared_ptr<lumex::block> deserialize_block_json (boost::property_tree::ptree const &, lumex::block_uniquer * = nullptr);
/**
 * Serialize a block prefixed with an 8-bit typecode
 */
void serialize_block (lumex::stream &, lumex::block const &);

void block_memory_pool_purge ();
}
