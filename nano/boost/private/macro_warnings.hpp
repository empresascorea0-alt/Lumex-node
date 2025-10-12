#pragma once

#ifdef _WIN32
#define DISABLE_ASIO_WARNINGS           \
	__pragma (warning (push))           \
	__pragma (warning (disable : 4191)) \
	__pragma (warning (disable : 4242))
#elif defined(__clang__) || defined(__GNUC__)
#define DISABLE_ASIO_WARNINGS \
	_Pragma ("GCC diagnostic push")
#else
#define DISABLE_ASIO_WARNINGS
#endif

#define DISABLE_BEAST_WARNINGS DISABLE_ASIO_WARNINGS

#ifdef _WIN32
#define DISABLE_PROCESS_WARNINGS        \
	__pragma (warning (push))           \
	__pragma (warning (disable : 4191)) \
	__pragma (warning (disable : 4242)) \
	__pragma (warning (disable : 4244))
#elif defined(__clang__) || defined(__GNUC__)
#define DISABLE_PROCESS_WARNINGS    \
	_Pragma ("GCC diagnostic push") \
	_Pragma ("GCC diagnostic ignored \"-Wdeprecated-declarations\"")
#else
#define DISABLE_PROCESS_WARNINGS
#endif

#ifdef _WIN32
#define REENABLE_WARNINGS \
	__pragma (warning (pop))
#elif defined(__clang__) || defined(__GNUC__)
#define REENABLE_WARNINGS \
	_Pragma ("GCC diagnostic pop")
#else
#define REENABLE_WARNINGS
#endif