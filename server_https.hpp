#ifndef SERVER_HTTPS_HPP
#define SERVER_HTTPS_HPP

#include "server_http.hpp"

#ifdef USE_STANDALONE_ASIO
#include <asio/ssl.hpp>
#else
#include <boost/asio/ssl.hpp>
#endif

#include <algorithm>
#include <openssl/ssl.h>

namespace SimpleWeb {
  typedef asio::ssl::stream<asio::ip::tcp::socket> HTTPS;

  template <>
  class Server<HTTPS> : public ServerBase<HTTPS> {
    Server(const Server &) = delete;
    Server &operator=(const Server &) = delete;

    std::string session_id_context;
    bool set_session_id_context = false;

  public:
    static std::shared_ptr<Server> create(const std::string &cert_file, const std::string &private_key_file, const std::string &verify_file = std::string()) {
      return std::shared_ptr<Server>(new Server(cert_file, private_key_file, verify_file));
    }

    void start() override {
      if(set_session_id_context) {
        // Creating session_id_context from address:port but reversed due to small SSL_MAX_SSL_SESSION_ID_LENGTH
        session_id_context = std::to_string(config.port) + ':';
        session_id_context.append(config.address.rbegin(), config.address.rend());
        SSL_CTX_set_session_id_context(context.native_handle(), reinterpret_cast<const unsigned char *>(session_id_context.data()),
                                       std::min<size_t>(session_id_context.size(), SSL_MAX_SSL_SESSION_ID_LENGTH));
      }
      ServerBase::start();
    }

  protected:
    Server(const std::string &cert_file, const std::string &private_key_file, const std::string &verify_file)
        : ServerBase<HTTPS>::ServerBase(443), context(asio::ssl::context::tlsv12) {
      context.use_certificate_chain_file(cert_file);
      context.use_private_key_file(private_key_file, asio::ssl::context::pem);

      if(verify_file.size() > 0) {
        context.load_verify_file(verify_file);
        context.set_verify_mode(asio::ssl::verify_peer | asio::ssl::verify_fail_if_no_peer_cert | asio::ssl::verify_client_once);
        set_session_id_context = true;
      }
    }

    asio::ssl::context context;

    void accept() override {
      //Create new socket for this connection
      //Shared_ptr is used to pass temporary objects to the asynchronous functions
      auto session = std::make_shared<Session>(this->shared_from_this(), std::make_shared<HTTPS>(*io_service, context));

      acceptor->async_accept(session->socket->lowest_layer(), [this, session](const error_code &ec) mutable {
        //Immediately start accepting a new connection (if io_service hasn't been stopped)
        if(ec != asio::error::operation_aborted)
          this->accept();


        if(!ec) {
          asio::ip::tcp::no_delay option(true);
          session->socket->lowest_layer().set_option(option);

          //Set timeout on the following asio::ssl::stream::async_handshake
          auto timer = this->get_timeout_timer(session, config.timeout_request);
          session->socket->async_handshake(asio::ssl::stream_base::server, [this, session, timer](const error_code &ec) mutable {
            if(timer)
              timer->cancel();
            if(!ec)
              this->read_request_and_content(session);
            else if(this->on_error)
              this->on_error(session->request, ec);
          });
        }
        else if(this->on_error)
          this->on_error(session->request, ec);
      });
    }
  };
} // namespace SimpleWeb

#endif /* SERVER_HTTPS_HPP */
