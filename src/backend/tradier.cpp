/*
beerja - stock screener

Copyright (C) 2020-2021  Ivan Gagis <igagis@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

/* ================ LICENSE END ================ */

#include "tradier.hpp"

#include <utki/string.hpp>

#include <httpc/request.hpp>
#include <httpc/util.hpp>

#include <jsondom/dom.hpp>

#include "../util/date.hpp"

const std::string tradier::tag = "tradier";

namespace{
const std::string end_point = "https://sandbox.tradier.com/v1/";
}

namespace{
struct tradier_async_operation : public beerja::async_operation{
	std::weak_ptr<httpc::request> http_req;

	bool cancel()override{
		if(auto r = this->http_req.lock()){
			return r->cancel();
		}
		return false; // http request object has been destroyed => http request has finished already => not cancelled
	}
};
}

void tradier::set_config(const treeml::forest& config){

	auto i = std::find(config.begin(), config.end(), "access_token");
	if(i != config.end()){
		if(!i->children.empty()){
			this->access_token = i->children.front().value.to_string();
		}
	}

	LOG([this](auto&o){o << "API Key = " << this->access_token << std::endl;})
}

std::shared_ptr<beerja::async_operation> tradier::get_exchanges(
			std::function<void(
					beerja::status,
					std::shared_ptr<beerja::async_operation>,
					std::vector<beerja::exchange>&&
				)>&& callback
		)
{
	if(!callback){
		throw std::logic_error("tradier::get_exchanges(): passed in callback is nullptr");
	}

	std::vector<beerja::exchange> ret = {
		{"A", "NYSE MKT" },
		{"B", "NASDAQ OMX BX" },
		{"C", "National Stock Exchange" },
		{"D", "FINRA ADF" },
		{"E", "Market Independent (Generated by Nasdaq SIP)" },
		{"F", "Mutual Funds/Money Markets (NASDAQ)" },
		{"I", "International Securities Exchange" },
		{"J", "Direct Edge A" },
		{"K", "Direct Edge X" },
		{"M", "Chicago Stock Exchange" },
		{"N", "NYSE" },
		{"P", "NYSE Arca" },
		{"Q", "NASDAQ OMX" },
		{"S", "NASDAQ Small Cap" },
		{"T", "NASDAQ Int" },
		{"U", "OTCBB" },
		{"V", "OTC other" },
		{"W", "CBOE" },
		{"X", "NASDAQ OMX PSX" },
		{"G", "GLOBEX" },
		{"Y", "BATS Y-Exchange" },
		{"Z", "BATS" }
	};
	
	auto asop = std::make_shared<tradier_async_operation>();

	callback(beerja::status::ok, asop, std::move(ret));

	return asop;
}

namespace{
std::vector<beerja::ticker> parse_ticker_list(const jsondom::value& json){
	if(!json.is_object()){
		ASSERT(false)
		return std::vector<beerja::ticker>();
	}

	auto& root = json.object();
	auto securities_i = root.find("securities");
	if(securities_i == root.end() || !securities_i->second.is_object()){
		return std::vector<beerja::ticker>();
	}

	auto& securities = securities_i->second.object();
	auto security_i = securities.find("security");
	if(security_i == securities.end() || !security_i->second.is_array()){
		return std::vector<beerja::ticker>();
	}
	
	auto& security = security_i->second.array();

	std::vector<beerja::ticker> ret;

	for(auto& s : security){
		if(!s.is_object()){
			continue;
		}

		auto& o = s.object();

		auto symbol_i = o.find("symbol");
		if(symbol_i == o.end() || !symbol_i->second.is_string()){
			continue;
		}
		auto& symbol = symbol_i->second.string();

		std::string description;
		auto description_i = o.find("description");
		if(description_i != o.end() && description_i->second.is_string()){
			description = description_i->second.string();
		}

		std::string exchange;
		auto exchange_i = o.find("exchange");
		if(exchange_i != o.end() && exchange_i->second.is_string()){
			exchange = exchange_i->second.string();
		}

		ret.push_back(beerja::ticker{
			.id = symbol,
			.name = std::move(description),
			.exchange_id = std::move(exchange)
		});
	}
	
	return ret;
}
}

std::shared_ptr<beerja::async_operation> tradier::find_ticker(
		const std::string& query,
		std::function<void(
				beerja::status,
				const std::shared_ptr<beerja::async_operation>&,
				std::vector<beerja::ticker>&&
			)>&& callback
	)
{
	if(!callback){
		throw std::logic_error("tradier::find_ticker(): passed in callback is nullptr");
	}

	auto asop = std::make_shared<tradier_async_operation>();

	auto r = std::make_shared<httpc::request>([callback, asop](httpc::status_code status_code, httpc::request& r){
		auto& resp = r.get_response();
		if(status_code != httpc::status_code::ok || resp.status != httpmodel::status::http_200_ok){
			LOG([&](auto&o){o << "status_code = " << unsigned(status_code) << " resp.status = " << unsigned(resp.status) << std::endl;})
			callback(beerja::status::failure, asop, std::vector<beerja::ticker>());
			return;
		}

		try{
			LOG([&](auto&o){o << "BODY = " << utki::make_string(resp.body) << std::endl;})
			auto json = jsondom::read(utki::make_span(resp.body));

			callback(beerja::status::ok, asop, parse_ticker_list(json));
		}catch(...){
			callback(beerja::status::failure, asop, std::vector<beerja::ticker>());
		}
	});

	asop->http_req = r;

	r->set_url(end_point + "markets/search?q=" + httpc::escape(query) + "&indexes=false");

	r->set_headers({
			{"Authorization", std::string("Bearer ") + this->access_token},
			{"Accept", "application/json"}
		});

	r->start();

	return asop;
}

namespace{
float get_float(const jsondom::value& v){
	if(v.is_number()){
		return v.number().to_float();
	}
	return -1;
}
}

namespace{
beerja::quote parse_quote(const jsondom::value& json){
	auto& quote = json.object().at("quotes").object().at("quote").object();

	beerja::quote ret;

	ret.last = quote.at("last").number().to_float();
	ret.change = quote.at("change").number().to_float();
	ret.change_percent = quote.at("change_percentage").number().to_float();

	ret.close = get_float(quote.at("close"));
	ret.open = get_float(quote.at("open"));
	ret.high = get_float(quote.at("high"));
	ret.low = get_float(quote.at("low"));

	ret.volume = quote.at("volume").number().to_uint64();
	
	return ret;
}
}

std::shared_ptr<beerja::async_operation> tradier::get_quote(
		const std::string& symbol,
		std::function<void(
				beerja::status,
				const std::shared_ptr<beerja::async_operation>&,
				beerja::quote
			)>&& callback
	)
{
	if(!callback){
		throw std::logic_error("tradier::get_quote(): passed in callback is nullptr");
	}

	auto asop = std::make_shared<tradier_async_operation>();

	auto r = std::make_shared<httpc::request>([callback, asop](httpc::status_code status_code, httpc::request& r){
		auto& resp = r.get_response();
		if(status_code != httpc::status_code::ok || resp.status != httpmodel::status::http_200_ok){
			LOG([&](auto&o){o << "status_code = " << unsigned(status_code) << " resp.status = " << unsigned(resp.status) << std::endl;})
			callback(beerja::status::failure, asop, beerja::quote());
			return;
		}

		try{
			LOG([&](auto&o){o << "BODY = " << utki::make_string(resp.body) << std::endl;})
			auto json = jsondom::read(utki::make_span(resp.body));

			callback(beerja::status::ok, asop, parse_quote(json));
		}catch(std::exception& e){
			LOG([&](auto&o){o << "parsing response failed: " << e.what() << std::endl;})
			callback(beerja::status::failure, asop, beerja::quote());
		}catch(...){
			LOG([](auto&o){o << "parsing response failed" << std::endl;})
			callback(beerja::status::failure, asop, beerja::quote());
		}
	});

	asop->http_req = r;

	r->set_url(end_point + "markets/quotes?symbols=" + httpc::escape(symbol) + "&greeks=false");

	r->set_headers({
			{"Authorization", std::string("Bearer ") + this->access_token},
			{"Accept", "application/json"}
		});

	r->start();

	return asop;
}

namespace{
::date::sys_seconds parse_datetime(const std::string& str){
	std::istringstream in(str);
	::date::sys_seconds tp;
	in >> ::date::parse("%FT%T", tp);
	return tp;
}
}

namespace{
std::vector<beerja::granule> parse_prices(const jsondom::value& json){
	auto& data = json.object().at("series").object().at("data").array();

	std::vector<beerja::granule> ret;

	for(auto& obj : data){
		auto& o = obj.object();

		ret.push_back(beerja::granule{
				.timestamp = parse_datetime(o.at("time").string()),
				.volume = o.at("volume").number().to_uint64(),
				.open = o.at("open").number().to_float(),
				.close = o.at("close").number().to_float(),
				.high = o.at("high").number().to_float(),
				.low = o.at("low").number().to_float(),
				.price = o.at("vwap").number().to_float()
			});
	}

	return ret;
}
}

std::shared_ptr<beerja::async_operation> tradier::get_prices(
		const std::string& symbol,
		::date::sys_time<std::chrono::minutes> from,
		::date::sys_time<std::chrono::minutes> to,
		granularity gran,
		std::function<void(
				beerja::status,
				const std::shared_ptr<beerja::async_operation>&,
				std::vector<beerja::granule>&& data
			)>&& callback
	)
{
	if(!callback){
		throw std::logic_error("tradier::get_quote(): passed in callback is nullptr");
	}

	auto asop = std::make_shared<tradier_async_operation>();

	if(gran == granularity::day){
		//TODO:
		ASSERT(false)
		return asop;
	}

	auto r = std::make_shared<httpc::request>([callback, asop](httpc::status_code status_code, httpc::request& r){
		auto& resp = r.get_response();
		if(status_code != httpc::status_code::ok || resp.status != httpmodel::status::http_200_ok){
			LOG([&](auto&o){o << "status_code = " << unsigned(status_code) << " resp.status = " << unsigned(resp.status) << std::endl;})
			callback(beerja::status::failure, asop, std::vector<beerja::granule>());
			return;
		}

		try{
			LOG([&](auto&o){o << "BODY = " << utki::make_string(resp.body) << std::endl;})
			auto json = jsondom::read(utki::make_span(resp.body));

			callback(beerja::status::ok, asop, parse_prices(json));
		}catch(...){
			LOG([](auto&o){o << "Error parsing prices" << std::endl;})
			callback(beerja::status::failure, asop, std::vector<beerja::granule>());
		}
	});

	asop->http_req = r;

	std::string interval;
	switch(gran){
		case granularity::minute:
			interval = "1min";
			break;
		case granularity::five_minutes:
			interval = "5min";
			break;
		case granularity::fivteen_minutes:
			interval = "15min";
			break;
		case granularity::day:
			ASSERT(false)
			break;
	}

	std::string start_time;
	std::string end_time;
	{
		using ::date::operator<<;
		using ::date::floor;
		{
			std::stringstream ss;
			ss << floor<std::chrono::minutes>(to);
			end_time = ss.str();
		}
		{
			std::stringstream ss;
			ss << floor<std::chrono::minutes>(backend::get_start_time(to, gran));
			start_time = ss.str();
		}
	}

	LOG([&](auto&o){o << "interval = " << interval << std::endl;})
	LOG([&](auto&o){o << "start_time = " << start_time << std::endl;})
	LOG([&](auto&o){o << "end_time = " << end_time << std::endl;})

	r->set_url(end_point + "markets/timesales?symbol=" + httpc::escape(symbol) +
			"&session_filter=open&interval=" + interval +
			"&start=" + httpc::escape(start_time) +
			"&end=" + httpc::escape(end_time)
		);

	r->set_headers({
			{"Authorization", std::string("Bearer ") + this->access_token},
			{"Accept", "application/json"}
		});

	r->start();

	return asop;
}
