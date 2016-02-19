// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Copyright (C) 2016 Red Hat Inc.
 */


#include <chrono>
#include <iostream>


#include "dmclock_server.h"
#include "dmclock_util.h"
#include "gtest/gtest.h"


namespace dmc = crimson::dmclock;


// we need a request object; an empty one will do
struct Request {
};


namespace crimson {
  namespace dmclock {

    /*
     * Allows us to test the code provided with the mutex provided locked.
     */
    static void test_locked(std::mutex& mtx, std::function<void()> code) {
      std::unique_lock<std::mutex> l(mtx);
      code();
    }

    
    TEST(dmclock_server, client_idle_erase) {
      using ClientId = int;
      int client = 17;

      dmc::ClientInfo ci(1.0, 100.0, 0.0);
      auto client_info_f = [&] (ClientId c) -> dmc::ClientInfo { return ci; };
      auto server_ready_f = [] () -> bool { return true; };
      auto submit_req_f = [] (const ClientId& c,
			      std::unique_ptr<Request> req,
			      dmc::PhaseType phase) {
      };

      dmc::PriorityQueue<ClientId,Request> pq(client_info_f,
					      server_ready_f,
					      submit_req_f,
					      false,
					      std::chrono::seconds(3),
					      std::chrono::seconds(5),
					      std::chrono::seconds(2));

      auto lock_pq = [&](std::function<void()> code) {
	test_locked(pq.data_mtx, code);
      };


      /* The timeline should be as follows:
       *
       *     0 seconds : request created
       *
       *     1 seconds : map is size 1, idle is false
       *
       * 2 seconds : clean notes first mark; +2 is base for further calcs
       *
       * 4 seconds : clean does nothing except makes another mark
       *
       *   5 seconds : when we're secheduled to idle (+2 + 3)
       *
       * 6 seconds : clean idles client
       *
       *   7 seconds : when we're secheduled to erase (+2 + 5)
       *
       *     7 seconds : verified client is idle
       *
       * 8 seconds : clean erases client info
       *
       *     9 seconds : verified client is erased
       */

      lock_pq([&] () {
	  EXPECT_EQ(pq.client_map.size(), 0) << "client map initially has size 0";
	});

      Request req;
      dmc::ReqParams<ClientId> req_params(client, 1, 1);
      pq.add_request(req, req_params, dmc::get_time());

      std::this_thread::sleep_for(std::chrono::seconds(1));

      lock_pq([&] () {
	  EXPECT_EQ(1, pq.client_map.size()) << "client map has 1 after 1 client";
	  EXPECT_FALSE(pq.client_map.at(client).idle) <<
	    "initially client map entry shows not idle.";
	});

      std::this_thread::sleep_for(std::chrono::seconds(6));

      lock_pq([&] () {
	  EXPECT_TRUE(pq.client_map.at(client).idle) <<
	    "after idle age client map entry shows idle.";
	});

      std::this_thread::sleep_for(std::chrono::seconds(2));

      lock_pq([&] () {
	  EXPECT_EQ(0, pq.client_map.size()) <<
	    "client map loses its entry after erase age";
	});
    }
  } // namespace dmclock
} // namespace crimson
