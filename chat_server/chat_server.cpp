#include <cstdlib>
#include <deque>
#include <iostream>
#include <list>
#include <memory>
#include <set>
#include <utility>
#include <boost/asio.hpp>

class chat_message
{
public:
	// big edian
	enum { header_length = 2};
	enum {max_body_length = 65535};

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

class chat_server;
class chat_session;

class chat_room
{
public:
	void join(std::shared_ptr<chat_session> session);
	void leave(std::shared_ptr<chat_session> session);
	void deliver(const chat_message& msg);
	
private:
	std::set<std::shared_ptr<chat_session>> sessions_;
	std::deque<chat_message> recent_msgs_;
	enum { max_recent_msgs = 100 };
};

class chat_session : public std::enable_shared_from_this<chat_session>
{
public:
	chat_session(boost::asio::ip::tcp::socket socket, chat_room& room)
		: socket_(std::move(socket)), room_(room)
	{
		room_.join(shared_from_this());
		do_read_header();
	}

	void deliver(const chat_message& msg)
	{
		write_msgs_.push_back(msg);
		do_write();
	}

private:
	void do_read_header()
	{
		auto p = shared_from_this();
		boost::asio::async_read(socket_, boost::asio::buffer(read_msg_.data(), chat_message::header_length),
			[this, p](boost::system::error_code ec, std::size_t len) {
			if (ec)
			{
				room_.leave(p);
			}
			else
			{
				read_msg_.decode_header();
				do_read_body();
			}
		});
	}
	
	void do_read_body()
	{
		auto p = shared_from_this();
		boost::asio::async_read(socket_, boost::asio::buffer(read_msg_.body(), read_msg_.body_length()),
			[p, this](boost::system::error_code ec, std::size_t len) {
			if (ec)
			{
				room_.leave(p);
			}
			else
			{
				room_.deliver(read_msg_);
				do_read_body();
			}
		});
	}

	void do_write()
	{

	}

	boost::asio::ip::tcp::socket socket_;
	chat_room& room_;
	chat_message read_msg_;
	std::deque<chat_message> write_msgs_;
};

class chat_server
{
public:
	chat_server(boost::asio::io_context& io_context,
		const boost::asio::ip::tcp::endpoint& endpoint)
		: acceptor_(io_context, endpoint)
	{
		do_accept();
	}

	void do_accept()
	{
		acceptor_.async_accept(
			[this](boost::system::error_code ec, boost::asio::ip::tcp::socket socket) {
			if (ec)
			{
				std::cout << ec.message() << std::endl;
			}
			auto p = std::make_shared<chat_session>(std::move(socket), room_);

			do_accept();
			; });
	}

private:
	boost::asio::ip::tcp::acceptor acceptor_;
	chat_room room_;
};

void chat_room::join(std::shared_ptr<chat_session> session)
{
	sessions_.insert(session);
	for (auto msg : recent_msgs_)
	{
		session->deliver(msg);
	}
}

void chat_room::deliver(const chat_message& msg)
{
	recent_msgs_.push_back(msg);
	while (recent_msgs_.size() > max_recent_msgs)
	{
		recent_msgs_.pop_front();
	}
	for (auto session : sessions_)
	{
		session->deliver(msg);
	}
}

void chat_room::leave(std::shared_ptr<chat_session> session)
{
	sessions_.erase(session);
}

int main(int argc, char* argv[])
{
	try
	{
		boost::asio::io_context io_context;
		boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::tcp::v4(), 8001);
		chat_server server(io_context, endpoint);
		io_context.run();

	}
	catch (const std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << "\n";
	}
	return 0;
}
