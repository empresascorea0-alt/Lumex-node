#pragma once

namespace lumex
{
class node;
}

namespace lumex::messages
{
class telemetry_data;
}

namespace lumex::test
{
/**
 * Compares telemetry data without signatures
 * @return true if comparison OK
 */
bool compare_telemetry_data (lumex::messages::telemetry_data const &, lumex::messages::telemetry_data const &);

/**
 * Compares telemetry data and checks signature matches node_id
 * @return true if comparison OK
 */
bool compare_telemetry (lumex::messages::telemetry_data const &, lumex::node const &);
}