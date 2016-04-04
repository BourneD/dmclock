// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

/*
 * Copyright (C) 2016 Red Hat Inc.
 */


#pragma once


#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <vector>
#include <deque>
#include <iostream>

#include "dmclock_recs.h"
#include "dmclock_client.h"

#include "test_simp_recs.h"


struct req_op_t {};
struct wait_op_t {};
constexpr struct req_op_t req_op {};
constexpr struct wait_op_t wait_op {};


enum class CliOp { req, wait };
struct CliInst {
  CliOp op;
  union {
    std::chrono::milliseconds wait_time;
    struct {
      uint16_t count;
      std::chrono::milliseconds time_bw_reqs;
      uint16_t max_outstanding;
    } req_params;
  } args;

  // D is a duration type
  template<typename D>
  CliInst(wait_op_t, D duration) :
    op(CliOp::wait)
  {
    args.wait_time =
      std::chrono::duration_cast<std::chrono::milliseconds>(duration);
  }

  CliInst(req_op_t,
	  uint16_t count, double ops_per_sec, uint16_t max_outstanding) :
    op(CliOp::req)
  {
    args.req_params.count = count;
    args.req_params.max_outstanding = max_outstanding;
    uint32_t ms = uint32_t(0.5 + 1.0 / ops_per_sec * 1000);
    args.req_params.time_bw_reqs = std::chrono::milliseconds(ms);
  }
};


// ServiceTrk = crimson::dmclock::ServiceTracker<ServerId>
// ReqPm = crimson::dmclock::ReqParams<ClientId>
// RespPm = crimson::dmclock::RespParams<ServerId>


template<typename SvcTrk, typename ReqPm, typename RespPm>
class TestClient {
public:

  using SubmitFunc =
    std::function<void(const ServerId&, const TestRequest&, const ReqPm&)>;

  using ServerSelectFunc = std::function<const ServerId&(uint64_t seed)>;

  typedef std::chrono::time_point<std::chrono::steady_clock> TimePoint;

  static TimePoint now() { return std::chrono::steady_clock::now(); }

protected:

  struct RespQueueItem {
    TestResponse response;
    RespPm resp_params;
  };

  const ClientId id;
  const SubmitFunc submit_f;
  const ServerSelectFunc server_select_f;

  std::vector<CliInst> instructions;

  SvcTrk service_tracker;

  // TODO: use lock rather than atomic???
  std::atomic_ulong        outstanding_ops;
  std::atomic_bool         requests_complete;

  std::deque<RespQueueItem> resp_queue;

  std::mutex               mtx_req;
  std::condition_variable  cv_req;

  std::mutex               mtx_resp;
  std::condition_variable  cv_resp;

  using RespGuard = std::lock_guard<decltype(mtx_resp)>;
  using Lock = std::unique_lock<std::mutex>;

  // data collection

  std::vector<TimePoint>   op_times;
  uint32_t                 reservation_counter = 0;
  uint32_t                 proportion_counter = 0;

  std::thread              thd_req;
  std::thread              thd_resp;

public:

  TestClient(ClientId _id,
	     const SubmitFunc& _submit_f,
	     const ServerSelectFunc& _server_select_f,
	     const std::vector<CliInst>& _instrs) :
    id(_id),
    submit_f(_submit_f),
    server_select_f(_server_select_f),
    instructions(_instrs),
    service_tracker(),
    outstanding_ops(0),
    requests_complete(false)
  {
    size_t op_count = 0;
    for (auto i : instructions) {
      if (CliOp::req == i.op) {
	op_count += i.args.req_params.count;
      }
    }
    op_times.reserve(op_count);

    thd_resp = std::thread(&TestClient::run_resp, this);
    thd_req = std::thread(&TestClient::run_req, this);
  }


  TestClient(ClientId _id,
	     const SubmitFunc& _submit_f,
	     const ServerSelectFunc& _server_select_f,
	     uint16_t _ops_to_run,
	     double _iops_goal,
	     uint16_t _outstanding_ops_allowed) :
    TestClient(_id,
	       _submit_f, _server_select_f,
	       {{req_op, _ops_to_run, _iops_goal, _outstanding_ops_allowed}})
  {
    // empty
  }


  TestClient(const TestClient&) = delete;
  TestClient(TestClient&&) = delete;
  TestClient& operator=(const TestClient&) = delete;
  TestClient& operator=(TestClient&&) = delete;

  virtual ~TestClient() {
    wait_until_done();
  }

  void receive_response(const TestResponse&,
			const RespPm&) {
  }

  const std::vector<TimePoint>& get_op_times() const { return op_times; }
  uint32_t get_res_count() const { return reservation_counter; }
  uint32_t get_prop_count() const { return proportion_counter; }

  void wait_until_done() {
    if (thd_req.joinable()) thd_req.join();
    if (thd_resp.joinable()) thd_resp.join();
  }

protected:

  void run_req() {
    size_t ops_count = 0;
    for (auto i : instructions) {
      if (CliOp::wait == i.op) {
	std::this_thread::sleep_for(i.args.wait_time);
      } else if (CliOp::req == i.op) {
	Lock l(mtx_req);
	for (uint64_t o = 0; o < i.args.req_params.count; ++o) {
	  while (outstanding_ops >= i.args.req_params.max_outstanding) {
	    cv_req.wait(l);
	  }

	  l.unlock();
	  auto now = std::chrono::steady_clock::now();
	  const ServerId& server = server_select_f(o);
	  crimson::dmclock::ReqParams<ClientId> rp =
	    service_tracker.get_req_params(id, server);
	  TestRequest req(server, o, 12);
	  submit_f(server, req, rp);
	  ++outstanding_ops;
	  l.lock(); // lock for return to top of loop

	  auto delay_time = now + i.args.req_params.time_bw_reqs;
	  while (std::chrono::steady_clock::now() < delay_time) {
	    cv_req.wait_until(l, delay_time);
	  } // while
	} // for
	ops_count += i.args.req_params.count;
      } else {
	assert(false);
      }
    } // for loop

    requests_complete = true;

    // all requests made, thread ends
  }

  
  void run_resp() {
    std::chrono::milliseconds delay(1000);
    int op = 0;

    Lock l(mtx_resp);

    // since the following code would otherwise be repeated (except for
    // the call to notify_one) in the two loops below; let's avoid
    // repetition and define it once.
    const auto proc_resp = [this, &op, &l](const bool notify_req_cv) {
      if (!resp_queue.empty()) {
	RespQueueItem item = resp_queue.front();
	resp_queue.pop_front();

	l.unlock();

	// data collection

	op_times.push_back(now());
	if (crimson::dmclock::PhaseType::reservation == item.resp_params.phase) {
	  ++reservation_counter;
	} else {
	  ++proportion_counter;
	}

	// processing

#if 0 // not needed
	TestResponse& resp = item.response;
#endif

	service_tracker.track_resp(item.resp_params);

	--outstanding_ops;
	if (notify_req_cv) {
	  cv_req.notify_one();
	}

	l.lock();
      }
    };

    while(!requests_complete.load()) {
      while(resp_queue.empty() && !requests_complete.load()) {
	cv_resp.wait_for(l, delay);
      }
      proc_resp(true);
    }

    while(outstanding_ops.load() > 0) {
      while(resp_queue.empty() && outstanding_ops.load() > 0) {
	cv_resp.wait_for(l, delay);
      }
      proc_resp(false); // don't call notify_one as all requests are complete
    }

    // all responses received, thread ends
  }
}; // class TestClient
