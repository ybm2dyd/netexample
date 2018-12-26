#include <iostream>
#include <string>
#include <memory>
#include <functional>
#include <memory>
#include <stdint.h>
#include <utility>
#include <deque>

#include <boost/asio.hpp>
#include <boost/thread/thread.hpp>

class chat_message
{
public:
	// big edian
	enum { header_length = 2 };
	enum { max_body_length = 65535 };

	char* data()
	{
		return data_;
	}

	std::size_t length() const
	{
		return header_length + body_length_;
	}

	std::size_t body_length() const
	{
		return body_length_;
	}

	void body_length(std::size_t length)
	{
		body_length_ = length;
	}

	char* body()
	{
		return data_ + header_length;
	}

	void decode_header()
	{
		memcpy(&body_length_, data_, header_length);
		body_length_ = boost::asio::detail::socket_ops::network_to_host_short(body_length_);
	}

	void encode_header()
	{
		unsigned short hs = boost::asio::detail::socket_ops::host_to_network_short(body_length_);
		memcpy(data_, &hs, header_length);
	}

private:
	char data_[header_length + max_body_length];
	unsigned short body_length_;
};

class session
{
public:
	session(boost::asio::ip::tcp::endpoint& endpoints, boost::asio::io_context& ioc) :
		socket_(ioc), io_context_(ioc)
	{
		do_connect(endpoints);
	}

	void do_connect(boost::asio::ip::tcp::endpoint& endpoints)
	{
		socket_.async_connect(endpoints,
			[this](boost::system::error_code ec) {
			if (!ec)
			{
				do_read_header();
			}
		});
		//boost::asio::async_connect(socket_, endpoints, 
		//	[this](boost::system::error_code ec) {
		//	if (!ec)
		//	{
		//		do_read_header();
		//	}
		//	 });
	}

	void do_read_header()
	{
		boost::asio::async_read(socket_, boost::asio::buffer(read_msg_.data(), chat_message::header_length),
			[this](boost::system::error_code ec, std::size_t len) {
			if (!ec)
			{
				read_msg_.decode_header();
				do_read_body();
			}
			else
			{
				socket_.close();
			}
		});
	}

	void do_read_body()
	{
		boost::asio::async_read(socket_, boost::asio::buffer(read_msg_.body(), read_msg_.body_length()),
			[this](boost::system::error_code ec, std::size_t len) {
			if (!ec)
			{
				std::cout << std::string(read_msg_.body(), read_msg_.body_length()) << std::endl;
				do_read_header();
			}
			else
			{
				socket_.close();
			}
		});
	}

	void write(const chat_message& msg)
	{
		boost::asio::post(io_context_, [this, msg]() {
			bool write_in_progress = !write_msgs_.empty();
			write_msgs_.push_back(msg);
			if (!write_in_progress)
			{
				do_write();
			}
		});
	}

	void do_write()
	{
		boost::asio::async_write(socket_, boost::asio::buffer(write_msgs_.front().data(), write_msgs_.front().length()),
			[this](boost::system::error_code ec, std::size_t len) {
			if (!ec)
			{
				write_msgs_.pop_front();
				if (!write_msgs_.empty())
				{
					do_write();
				}
			}
			else
			{
				socket_.close();
			}
		});
	}

private:
	boost::asio::ip::tcp::socket socket_;
	std::deque<chat_message> write_msgs_;
	chat_message read_msg_;
	boost::asio::io_context& io_context_;
};

int main(int argc, const char* argv[])
{
	boost::asio::io_context io_context;
	boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::address::from_string("127.0.0.1"), 8001);
	session client(endpoint, io_context);

	boost::thread t([&io_context]() {io_context.run(); });

	char tempbuff[chat_message::max_body_length + 1];
	while (std::cin.getline(tempbuff, chat_message::max_body_length + 1))
	{
		chat_message msg;
		msg.body_length(strlen(tempbuff));
		msg.encode_header();
		memcpy(msg.body(), tempbuff, msg.body_length());
		client.write(msg);
	}

	t.join();
	return 0;
}