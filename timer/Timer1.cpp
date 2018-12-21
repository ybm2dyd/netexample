#include <iostream>
#include <boost/asio.hpp>
#include <boost/bind.hpp>

void Print(const boost::system::error_code& ec, boost::asio::steady_timer* timer, int* count)
{
	if (*count <= 5)
	{
		std::cout << "timer: " << *count << std::endl;
		++(*count);
		timer->expires_at(timer->expiry() + boost::asio::chrono::seconds(1));
		timer->async_wait(boost::bind(Print, boost::asio::placeholders::error, timer, count));
	}
}

int main()
{
	boost::asio::io_context io;
	boost::asio::io_context::work worker(io);
	boost::asio::steady_timer timer(io, boost::asio::chrono::seconds(1));
	auto p = std::make_shared<boost::asio::steady_timer>(io, boost::asio::chrono::seconds(1));
	int count = 1;
	timer.async_wait(boost::bind(Print, boost::asio::placeholders::error, &timer, &count));
	io.run();
}