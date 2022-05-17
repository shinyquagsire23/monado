// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief JSON C++ wrapper tests.
 * @author Mateo de Mayo <mateo.demayo@collabora.com>
 */


#include "catch/catch.hpp"
#include "util/u_json.hpp"
#include <string>

using std::string;
using xrt::auxiliary::util::json::JSONBuilder;
using xrt::auxiliary::util::json::JSONNode;

TEST_CASE("u_json")
{
	// This is the json data we will be dealing with
	// {
	//  "alpha": [1, true, 3.14, {"beta" : 4, "gamma" : 5}, {"delta" : 6}, [{"epsilon": [7], "zeta": false}]],
	//  "eta": "theta",
	//  "iota": {"kappa": [{"lambda": [5.5, [4.4, 3.3], {}, 2.2, 1, 0, {}, [-1], -2.2, -3.3, -4.4, -5.5]}]},
	//  "mu" : true,
	//  "nu" : false,
	//  "xi": 42,
	//  "omicron": [],
	//  "pi": 3.141592,
	//  "rho": [{"sigma": [{ "tau": [{"upsilon": [[[]]]}]}]}]
	// }

	JSONBuilder jb{};
	jb << "{";
	jb << "alpha"
	   << "[" << 1 << true << 3.14 << "{"
	   << "beta" << 4 << "gamma" << 5 << "}"
	   << "{"
	   << "delta" << 6 << "}"
	   << "["
	   << "{"
	   << "epsilon"
	   << "[" << 7 << "]"
	   << "zeta" << false << "}"
	   << "]"
	   << "]";
	jb << "eta"
	   << "theta";
	jb << "iota"
	   << "{"
	   << "kappa"
	   << "["
	   << "{"
	   << "lambda"
	   << "[" << 5.5 << "[" << 4.4 << 3.3 << "]"
	   << "{"
	   << "}" << 2.2 << 1 << 0 << "{"
	   << "}"
	   << "[" << -1 << "]" << -2.2 << -3.3 << -4.4 << -5.5 << "]"
	   << "}"
	   << "]"
	   << "}";
	jb << "mu" << true;
	jb << "nu" << false;
	jb << "xi" << 42;
	jb << "omicron"
	   << "["
	   << "]";
	jb << "pi" << 3.141592;
	jb << "rho"
	   << "["
	   << "{"
	   << "sigma"
	   << "["
	   << "{"
	   << "tau"
	   << "["
	   << "{"
	   << "upsilon"
	   << "["
	   << "["
	   << "["
	   << "]"
	   << "]"
	   << "]"
	   << "}"
	   << "]"
	   << "}"
	   << "]"
	   << "}"
	   << "]";
	jb << "}";

	JSONNode json_node = *jb.getBuiltNode();

	SECTION("JSONBuilder builds as expected")
	{
		string raw_json =
		    "{"
		    "\"alpha\": [1, true, 3.14, {\"beta\" : 4, \"gamma\" : 5}, {\"delta\" : 6}, [{\"epsilon\": [7], "
		    "\"zeta\": "
		    "false}]],"
		    "\"eta\": \"theta\","
		    "\"iota\": {\"kappa\": [{\"lambda\": [5.5, [4.4, 3.3], {}, 2.2, 1, 0, {}, [-1], -2.2, -3.3, -4.4, "
		    "-5.5]}]},"
		    "\"mu\" : true,"
		    "\"nu\" : false,"
		    "\"xi\": 42,"
		    "\"omicron\": [],"
		    "\"pi\": 3.141592,"
		    "\"rho\": [{\"sigma\": [{ \"tau\": [{\"upsilon\": [[[]]]}]}]}]"
		    "}";
		JSONNode node_from_string{raw_json};
		INFO("json_node=" << json_node.toString(false));
		INFO("node_from_string=" << node_from_string.toString(false));
		CHECK(json_node.toString(false) == node_from_string.toString(false));
	}

	SECTION("Complex JSON is preserved through save and load")
	{
		JSONNode loaded{jb.getBuiltNode()->toString(false)};
		INFO("json_node=" << json_node.toString(false));
		INFO("loaded=" << loaded.toString(false));
		CHECK(json_node.toString(false) == loaded.toString(false));
	}

	SECTION("Read methods work")
	{
		CHECK(json_node["eta"].asString() == "theta");
		CHECK(json_node["eta"].asDouble(3.14) == 3.14);
		CHECK(json_node["mu"].asBool(false) == true);
		CHECK(json_node["alpha"][0].canBool());
		CHECK(json_node["alpha"][0].asBool(false) == true);
		CHECK(json_node["alpha"][4]["delta"].asInt(-1) == 6);
		CHECK(json_node["alpha"][4]["delta"].asDouble(-1) == 6.0);
		CHECK(json_node["rho"][0]["sigma"][0]["tau"][0]["upsilon"].asArray().size() == 1);
		CHECK(json_node["alpha"][3].asObject()["gamma"].asInt() == 5);
		CHECK(json_node["iota"].hasKey("kappa"));
	}
}
