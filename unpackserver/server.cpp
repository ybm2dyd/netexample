#include <iostream>
#include <string>
#include <memory>
#include <functional>
#include <memory>
#include <stdint.h>
#include "boost/asio.hpp"

struct Package
{
	uint16_t length;
	char ud[65536];
};

class Session : public std::enable_shared_from_this<Session>
{
	friend class Server;

public:
	Session(boost::asio::io_context& ioc) :
		socket_(ioc)
	{
	}

	void RecvAndWrite()
	{
		auto p = shared_from_this();
		boost::asio::async_read(socket_, boost::asio::buffer(buffer_, 2),
			[p, this](boost::system::error_code ec, std::size_t len) {
			if (ec)
			{
				socket_.close();
				return;
			}
			uint16_t ns;
			memcpy(&ns, buffer_, 2);
			uint16_t hs = boost::asio::detail::socket_ops::network_to_host_short(ns);
			boost::asio::async_read(socket_, boost::asio::buffer(buffer_, hs),
				[p, this](boost::system::error_code ec, std::size_t len) {
				if (ec)
				{
					socket_.close();
					return;
				}
				uint16_t nsize = boost::asio::detail::socket_ops::host_to_network_short(len);
				std::cout << "recved" << std::string(buffer_, len) << std::endl;
				memcpy(sendbuffer_, &nsize, 2);
				memcpy(sendbuffer_ + 2, buffer_, len);
				boost::asio::async_write(socket_, boost::asio::buffer(sendbuffer_, len + 2),
					[p, this](boost::system::error_code ec, std::size_t len) {
					if (ec)
					{
						socket_.close();
						return;
					}
				});
				RecvAndWrite();
			});
		});
	}

private:
	boost::asio::ip::tcp::socket socket_;
	char buffer_[65536];
	char sendbuffer_[65536];
};

class Server
{
public:
	Server(boost::asio::io_context& ioc, boost::asio::ip::tcp::endpoint ep)
		: io_context_(ioc), acceptor_(ioc, ep)
	{
		std::cout << "listen: " << ep << std::endl;
	}

	void Accept()
	{
		auto p = std::make_shared<Session>(io_context_);
		acceptor_.async_accept(p->socket_, 
			[p, this](boost::system::error_code ec) {
			if (ec)
			{
				std::cout << ec.message() << std::endl;
			}
			else
			{
				std::cout << p->socket_.remote_endpoint() << " connected" << std::endl;
				p->RecvAndWrite();
				Accept();
			}
		});
	}

private:
	boost::asio::io_context& io_context_;
	boost::asio::ip::tcp::acceptor acceptor_;
};

int main(int argc, const char* argv[])
{
	boost::asio::io_context ioc(1);
	boost::asio::ip::tcp::endpoint ep(boost::asio::ip::address::from_string("127.0.0.1"), 8001);
	auto p = std::make_shared<Server>(ioc, ep);
	p->Accept();

	ioc.run();
	return 0;
}