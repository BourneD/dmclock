// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Copyright (C) 2015 Red Hat Inc.
 */


#include <unistd.h>
#include <iostream>

// #include "dmclock_util.h"
#include "dmclock_recs.h"
#include "test_server.h"


using namespace std::placeholders;

namespace dmc = crimson::dmclock;
using dmc::RespParams;


static const bool info = false;


TestServer::TestServer(int _id,
		       int _iops,
		       int _thread_pool_size,
		       const std::function<ClientInfo(int)>& _client_info_f,
		       const ClientResponseFunc& _client_resp_f) :
  id(_id),
  priority_queue(_client_info_f,
		 std::bind(&TestServer::hasAvailThread, this),
		 std::bind(&TestServer::innerPost, this, _1, _2, _3)),
  client_resp_f(_client_resp_f),
  iops(_iops),
  thread_pool_size(_thread_pool_size),
  finishing(false)
{
  op_time =
    std::chrono::microseconds((int) (0.5 +
				     thread_pool_size * 1000000.0 / iops));

  std::chrono::milliseconds delay(1000);
  threads = new std::thread[thread_pool_size];
  for (int i = 0; i < thread_pool_size; ++i) {
    threads[i] = std::thread(&TestServer::run, this, delay);
  }
}


TestServer::~TestServer() {
  Lock l(inner_queue_mtx);
  finishing = true;
  inner_queue_cv.notify_all();
  l.unlock();

  for (int i = 0; i < thread_pool_size; ++i) {
    threads[i].join();
  }

  delete[] threads;
}


void TestServer::run(std::chrono::milliseconds wait_delay) {
  Lock l(inner_queue_mtx);
  while(true) {
    while(inner_queue.empty() && !finishing) {
      inner_queue_cv.wait_for(l, wait_delay);
    }
    if (!inner_queue.empty()) {
      auto& front = inner_queue.front();
      auto client = front.client;
      auto req = std::move(front.request);
      auto phase = front.phase;
      inner_queue.pop_front();

      l.unlock();

      if (info > 3) {
	std::cout << "start req " << client << std::endl;
      }

      // simulation operation by sleeping; then call function to
      // notify server of completion
      std::this_thread::sleep_for(op_time);

      TestResponse resp(req->epoch, RespParams<int>(id, phase));
      sendResponse(client, resp);

      priority_queue.requestCompleted();

      if (info > 3) {
	std::cout << "end req " << client << std::endl;
      }

      l.lock(); // in prep for next iteration of loop
    } else {
      break;
    }
  }
}


void TestServer::post(const TestRequest& request,
		      const dmc::ReqParams<int>& req_params) {
  auto now = dmc::getTime();
  priority_queue.addRequest(request, req_params, now);
}


bool TestServer::hasAvailThread() {
  InnerQGuard g(inner_queue_mtx);
  return inner_queue.size() <= thread_pool_size;
}


void TestServer::innerPost(const int& client,
			   std::unique_ptr<TestRequest> request,
			   PhaseType phase) {
  Lock l(inner_queue_mtx);
  assert(!finishing);
  inner_queue.emplace_back(QueueItem(client, std::move(request), phase));
  inner_queue_cv.notify_one();
}
