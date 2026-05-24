#include <lumex/boost/beast/core/flat_buffer.hpp>
#include <lumex/boost/beast/http.hpp>
#include <lumex/lib/block_type.hpp>
#include <lumex/lib/blocks.hpp>
#include <lumex/lib/interval.hpp>
#include <lumex/lib/logging.hpp>
#include <lumex/lib/stats.hpp>
#include <lumex/node/election_status.hpp>
#include <lumex/node/node.hpp>
#include <lumex/node/node_observers.hpp>
#include <lumex/node/nodeconfig.hpp>
#include <lumex/node/rpc_callbacks.hpp>
#include <lumex/node/vote_with_weight_info.hpp>
#include <lumex/secure/ledger.hpp>

#include <boost/property_tree/json_parser.hpp>

lumex::http_callbacks::http_callbacks (lumex::node & node_a) :
	node{ node_a },
	config{ node_a.config },
	observers{ node_a.observers },
	ledger{ node_a.ledger },
	logger{ node_a.logger },
	stats{ node_a.stats },
	workers{ 1, lumex::thread_role::name::http_callbacks }
{
	// Only set up callbacks if a callback address is configured
	if (!config.callback_address.empty ())
	{
		logger.info (lumex::log::type::http_callbacks, "Callbacks enabled on {}:{}", config.callback_address, config.callback_port);
		setup_callbacks ();
	}
}

void lumex::http_callbacks::start ()
{
	workers.start ();
}

void lumex::http_callbacks::stop ()
{
	workers.stop ();
}

lumex::container_info lumex::http_callbacks::container_info () const
{
	return workers.container_info ();
}

void lumex::http_callbacks::setup_callbacks ()
{
	// Add observer for block confirmations
	observers.blocks.add ([this] (lumex::election_status const & status_a,
						  std::vector<lumex::vote_with_weight_info> const & votes_a,
						  lumex::account const & account_a,
						  lumex::amount const & amount_a,
						  bool is_state_send_a,
						  bool is_state_epoch_a) {
		auto block_a = status_a.winner;

		// Only process blocks that have achieved quorum or confirmation height
		if ((status_a.type == lumex::election_status_type::active_confirmed_quorum || status_a.type == lumex::election_status_type::active_confirmation_height))
		{
			stats.inc (lumex::stat::type::http_callbacks_notified, lumex::stat::detail::block_confirmed);

			if (workers.queued_tasks () > lumex::queue_warning_threshold () && warning_interval.elapse (15s))
			{
				stats.inc (lumex::stat::type::http_callbacks, lumex::stat::detail::large_backlog);
				logger.warn (lumex::log::type::http_callbacks, "Backlog of {} http callback notifications to process", workers.queued_tasks ());
			}

			// Post callback processing to worker thread
			// Safe to capture 'this' by reference as workers are stopped before this component destruction
			workers.post ([this, block_a, account_a, amount_a, is_state_send_a, is_state_epoch_a] () {
				// Construct the callback payload as a property tree
				boost::property_tree::ptree event;
				event.add ("account", account_a.to_account ());
				event.add ("hash", block_a->hash ().to_string ());
				std::string block_text;
				block_a->serialize_json (block_text);
				event.add ("block", block_text);
				event.add ("amount", amount_a.to_string_dec ());

				// Add transaction type information
				if (is_state_send_a)
				{
					event.add ("is_send", is_state_send_a);
					event.add ("subtype", "send");
				}

				// Handle different state block subtypes
				else if (block_a->type () == lumex::block_type::state)
				{
					if (block_a->is_change ())
					{
						event.add ("subtype", "change");
					}
					else if (is_state_epoch_a)
					{
						debug_assert (amount_a == 0 && ledger.is_epoch_link (block_a->link_field ().value ()));
						event.add ("subtype", "epoch");
					}
					else
					{
						event.add ("subtype", "receive");
					}
				}

				// Serialize the event to JSON
				std::stringstream ostream;
				boost::property_tree::write_json (ostream, event);
				ostream.flush ();

				// Prepare callback request parameters
				auto body = std::make_shared<std::string> (ostream.str ());
				auto address = config.callback_address;
				auto port = config.callback_port;
				auto target = std::make_shared<std::string> (config.callback_target);
				auto resolver = std::make_shared<boost::asio::ip::tcp::resolver> (node.io_ctx);

				// Resolve the callback address
				// Safe to capture 'this' as io_context is stopped before node destruction
				resolver->async_resolve (address, std::to_string (port),
				[this, address, port, target, body, resolver] (boost::system::error_code const & ec,
				boost::asio::ip::tcp::resolver::results_type results) {
					if (!ec)
					{
						auto results_ptr = std::make_shared<boost::asio::ip::tcp::resolver::results_type> (std::move (results));
						do_rpc_callback (results_ptr->begin (), results_ptr->end (), address, port, target, body, resolver, results_ptr);
					}
					else
					{
						stats.inc (lumex::stat::type::http_callbacks, lumex::stat::detail::error_resolving);
						stats.inc (lumex::stat::type::http_callbacks_ec, to_stat_detail (ec));

						logger.error (lumex::log::type::http_callbacks, "Error resolving callback: {}:{} ({})", address, port, ec.message ());
					}
				});
			});
		}
	});
}

/**
 * Performs the actual RPC callback HTTP request
 * Handles connection establishment, request sending, and response processing
 * Includes retry logic for failed connection attempts
 */
void lumex::http_callbacks::do_rpc_callback (boost::asio::ip::tcp::resolver::results_type::iterator i_a,
boost::asio::ip::tcp::resolver::results_type::iterator end_a,
std::string const & address,
uint16_t port,
std::shared_ptr<std::string> const & target,
std::shared_ptr<std::string> const & body,
std::shared_ptr<boost::asio::ip::tcp::resolver> const & resolver,
std::shared_ptr<boost::asio::ip::tcp::resolver::results_type> const & results)
{
	// Check if we have more endpoints to try
	if (i_a != end_a)
	{
		stats.inc (lumex::stat::type::http_callbacks, lumex::stat::detail::initiate);

		// Create socket and attempt connection
		auto sock = std::make_shared<boost::asio::ip::tcp::socket> (node.io_ctx);
		sock->async_connect (i_a->endpoint (),
		[this, target, body, sock, address, port, i_a, end_a, resolver, results] (boost::system::error_code const & ec) mutable {
			if (!ec)
			{
				// Connection successful, prepare and send HTTP request
				auto req = std::make_shared<boost::beast::http::request<boost::beast::http::string_body>> ();
				req->method (boost::beast::http::verb::post);
				req->target (*target);
				req->version (11);
				req->insert (boost::beast::http::field::host, address);
				req->insert (boost::beast::http::field::content_type, "application/json");
				req->body () = *body;
				req->prepare_payload ();

				// Send the HTTP request
				boost::beast::http::async_write (*sock, *req,
				[this, sock, address, port, req, i_a, end_a, target, body, resolver, results] (
				boost::system::error_code const & ec, std::size_t bytes_transferred) mutable {
					if (!ec)
					{
						// Request sent successfully, prepare to receive response
						auto sb = std::make_shared<boost::beast::flat_buffer> ();
						auto resp = std::make_shared<boost::beast::http::response<boost::beast::http::string_body>> ();

						// Read the HTTP response
						boost::beast::http::async_read (*sock, *sb, *resp,
						[this, sb, resp, sock, address, port, i_a, end_a, target, body, resolver, results] (
						boost::system::error_code const & ec, std::size_t bytes_transferred) mutable {
							if (!ec)
							{
								// Check response status
								if (boost::beast::http::to_status_class (resp->result ()) == boost::beast::http::status_class::successful)
								{
									stats.inc (lumex::stat::type::http_callbacks, lumex::stat::detail::success);
								}
								else
								{
									stats.inc (lumex::stat::type::http_callbacks, lumex::stat::detail::bad_status);

									logger.error (lumex::log::type::http_callbacks, "Callback to {}:{} failed [status: {}]",
									address, port, lumex::util::to_str (resp->result ()));
								}
							}
							else
							{
								stats.inc (lumex::stat::type::http_callbacks, lumex::stat::detail::error_completing);
								stats.inc (lumex::stat::type::http_callbacks_ec, to_stat_detail (ec));

								logger.error (lumex::log::type::http_callbacks, "Unable to complete callback: {}:{} ({})",
								address, port, ec.message ());
							}
						});
					}
					else
					{
						stats.inc (lumex::stat::type::http_callbacks, lumex::stat::detail::error_sending);
						stats.inc (lumex::stat::type::http_callbacks_ec, to_stat_detail (ec));

						logger.error (lumex::log::type::http_callbacks, "Unable to send callback: {}:{} ({})", address, port, ec.message ());
					}
				});
			}
			else // Connection failed, try next endpoint if available
			{
				stats.inc (lumex::stat::type::http_callbacks, lumex::stat::detail::error_connecting);
				stats.inc (lumex::stat::type::http_callbacks_ec, to_stat_detail (ec));

				logger.error (lumex::log::type::http_callbacks, "Unable to connect to callback address({}): {}:{} ({})",
				address, i_a->endpoint ().address ().to_string (), port, ec.message ());

				++i_a;
				do_rpc_callback (i_a, end_a, address, port, target, body, resolver, results);
			}
		});
	}
}