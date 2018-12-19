#include <iostream>
#include <string>
#include <memory>
#include <functional>
#include "boost/asio.hpp"

#define BUFFER_SIZE 65536

class Session : public std::enable_shared_from_this<Session>
{
	friend class Server;
public:
	Session(boost::asio::io_context& ioc) : socket_(ioc)
	{
	}

	void Start()
	{
		RecvAndSend();
	}

	void RecvAndSend()
	{
		auto p = shared_from_this();
		socket_.async_read_some(boost::asio::buffer(buffer_, sizeof(buffer_)),
			[p, this](boost::system::error_code err, std::size_t len) {
			if (err)
			{
				std::cout << err.message() << std::endl;
				return;
			}
			std::cout << "recved" << std::string(buffer_, len) << "\n";
			boost::asio::async_write(socket_, boost::asio::buffer(buffer_, len),
				[p, this](boost::system::error_code err, std::size_t len) {
				if (err) return;
			});
			RecvAndSend();
		});
	}

private:
	boost::asio::ip::tcp::socket socket_;
	char buffer_[BUFFER_SIZE];
};

class Server
{
public:
	Server(boost::asio::io_context& ioc, const boost::asio::ip::tcp::endpoint ep) : io_context_(ioc), acceptor_(ioc, ep)
	{
		std::cout << "listen: " << ep << std::endl;
	}

	void Accept()
	{
		auto p = std::make_shared<Session>(io_context_);
		acceptor_.async_accept(p->socket_,
			[this, p](boost::system::error_code err) {
			if (err)
			{
				std::cout << err << std::endl;
			}
			std::cout << "new conn, from ip:" << p->socket_.remote_endpoint().address().to_string() << "\n";
			p->Start();
			Accept();
		});
	}

private:
	boost::asio::ip::tcp::acceptor acceptor_;
	boost::asio::io_context& io_context_;
};

int main(int argc, const char* argv[])
{
	boost::asio::io_context ioc;

	boost::asio::ip::tcp::endpoint ep(boost::asio::ip::address::from_string("127.0.0.1"), 8001);
	auto p = std::make_shared<Server>(ioc, ep);
	p->Accept();

	ioc.run();
	return 0;
}

