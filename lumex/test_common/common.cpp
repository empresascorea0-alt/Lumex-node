#include <lumex/lib/logging.hpp>
#include <lumex/lib/stats.hpp>
#include <lumex/test_common/common.hpp>

lumex::logger & lumex::test::default_logger ()
{
	static lumex::logger logger{ "tests" };
	return logger;
}

lumex::stats & lumex::test::default_stats ()
{
	static lumex::stats stats{ lumex::test::default_logger () };
	return stats;
}