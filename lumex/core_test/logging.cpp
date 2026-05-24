#include <lumex/lib/logging.hpp>
#include <lumex/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <ostream>

using namespace std::chrono_literals;

namespace
{
struct non_copyable
{
	non_copyable () = default;
	non_copyable (non_copyable const &) = delete;
	non_copyable (non_copyable &&) = default;
	non_copyable & operator= (non_copyable const &) = delete;
	non_copyable & operator= (non_copyable &&) = default;

	friend std::ostream & operator<< (std::ostream & os, non_copyable const & nc)
	{
		os << "non_copyable";
		return os;
	}
};
}

TEST (tracing, no_copy)
{
	non_copyable nc;

	lumex::logger logger;
	logger.trace (lumex::log::type::test, lumex::log::detail::test, lumex::log::arg{ "non_copyable", nc });
}

namespace
{
struct non_moveable
{
	non_moveable () = default;
	non_moveable (non_moveable const &) = delete;
	non_moveable (non_moveable &&) = delete;
	non_moveable & operator= (non_moveable const &) = delete;
	non_moveable & operator= (non_moveable &&) = delete;

	friend std::ostream & operator<< (std::ostream & os, non_moveable const & nm)
	{
		os << "non_moveable";
		return os;
	}
};
}

TEST (tracing, no_move)
{
	non_moveable nm;

	lumex::logger logger;
	logger.trace (lumex::log::type::test, lumex::log::detail::test, lumex::log::arg{ "non_moveable", nm });
}

TEST (log_parse, parse_level)
{
	ASSERT_EQ (lumex::log::parse_level ("error"), lumex::log::level::error);
	ASSERT_EQ (lumex::log::parse_level ("off"), lumex::log::level::off);
	ASSERT_THROW (lumex::log::parse_level ("enumnotpresent"), std::invalid_argument);
	ASSERT_THROW (lumex::log::parse_level (""), std::invalid_argument);
	ASSERT_THROW (lumex::log::parse_level ("_last"), std::invalid_argument);
	ASSERT_THROW (lumex::log::parse_level ("_error"), std::invalid_argument);
}

TEST (log_parse, parse_type)
{
	ASSERT_EQ (lumex::log::parse_type ("node"), lumex::log::type::node);
	ASSERT_THROW (lumex::log::parse_type ("enumnotpresent"), std::invalid_argument);
	ASSERT_THROW (lumex::log::parse_type (""), std::invalid_argument);
	ASSERT_THROW (lumex::log::parse_type ("_last"), std::invalid_argument);
	ASSERT_THROW (lumex::log::parse_type ("_node"), std::invalid_argument);
}

TEST (log_parse, parse_detail)
{
	ASSERT_EQ (lumex::log::parse_detail ("all"), lumex::log::detail::all);
	ASSERT_EQ (lumex::log::parse_detail ("test"), lumex::log::detail::test);
	ASSERT_THROW (lumex::log::parse_detail ("enumnotpresent"), std::invalid_argument);
	ASSERT_THROW (lumex::log::parse_detail (""), std::invalid_argument);
	ASSERT_THROW (lumex::log::parse_detail ("_last"), std::invalid_argument);
	ASSERT_THROW (lumex::log::parse_detail ("_all"), std::invalid_argument);
}

TEST (log_parse, parse_logger_id)
{
	ASSERT_EQ (lumex::log::parse_logger_id ("node"), std::make_pair (lumex::log::type::node, lumex::log::detail::all));
	ASSERT_EQ (lumex::log::parse_logger_id ("node::all"), std::make_pair (lumex::log::type::node, lumex::log::detail::all));
	ASSERT_EQ (lumex::log::parse_logger_id ("node::test"), std::make_pair (lumex::log::type::node, lumex::log::detail::test));
	ASSERT_THROW (lumex::log::parse_logger_id ("_last"), std::invalid_argument);
	ASSERT_THROW (lumex::log::parse_logger_id ("node::enumnotpresent"), std::invalid_argument);
	ASSERT_THROW (lumex::log::parse_logger_id ("node::"), std::invalid_argument);
	ASSERT_THROW (lumex::log::parse_logger_id ("node::_all"), std::invalid_argument);
	ASSERT_THROW (lumex::log::parse_logger_id ("enumnotpresent"), std::invalid_argument);
	ASSERT_THROW (lumex::log::parse_logger_id ("invalid."), std::invalid_argument);
	ASSERT_THROW (lumex::log::parse_logger_id ("invalid._all"), std::invalid_argument);
	ASSERT_THROW (lumex::log::parse_logger_id ("::"), std::invalid_argument);
	ASSERT_THROW (lumex::log::parse_logger_id ("::all"), std::invalid_argument);
	ASSERT_THROW (lumex::log::parse_logger_id (""), std::invalid_argument);
}