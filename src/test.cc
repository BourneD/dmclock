// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Copyright (C) 2015 Red Hat Inc.
 */

#include <unistd.h>

#include <memory>
#include <chrono>
#include <iostream>

#include "test_recs.h"
#include "test_server.h"
#include "test_client.h"


using namespace std::placeholders;

namespace dmc = crimson::dmclock;
namespace chrono = std::chrono;


TestServer* testServer;


typedef std::unique_ptr<TestRequest> TestRequestRef;

std::mutex cout_mtx;
typedef typename std::lock_guard<std::mutex> Guard;


#define COUNT(array) (sizeof(array) / sizeof(array[0]))


static const int goal_secs_to_run = 30;

static const int server_ops = 150;
static const int server_threads = 7;

static const int client_outstanding_ops = 10;

static dmc::ClientInfo client_info[] = {
  // as of C++ 11 this will invoke the constructor with three doubles
  // as parameters
  {1.0, 50.0, 200.0},
  {2.0, 50.0, 200.0},
  // {1.0, 50.0, 0.0},
  // {2.0, 50.0, 0.0},
  // {2.0, 50.0, 0.0},
};

static int client_goals[] = {
  100,
  100,
  // 40,
  // 80,
  // 80,
}; // in IOPS


dmc::ClientInfo getClientInfo(int c) {
  assert(c < COUNT(client_info));
  return client_info[c];
}


void send_response(TestClient** clients,
		   ClientId client_id,
		   const TestResponse& resp,
		   const dmc::RespParams<ServerId>& resp_params) {
  clients[client_id]->receiveResponse(resp, resp_params);
}


int main(int argc, char* argv[]) {
  std::cout << "simulation started" << std::endl;

  const TestClient::TimePoint early_time = TestClient::now();
  const chrono::seconds skip_amount(2); // skip first 2 secondsd of data
  const chrono::seconds measure_unit(5); // calculate in groups of 5 seconds
  const chrono::seconds report_unit(1); // unit to output reports in

  assert(COUNT(client_info) == COUNT(client_goals));
  const int client_count = COUNT(client_info);

  TestClient** clients = new TestClient*[client_count];

  auto client_info_f = std::function<dmc::ClientInfo(ClientId)>(getClientInfo);
  TestServer::ClientRespFunc client_response_f =
    std::bind(&send_response, clients, _1, _2, _3);

  TestServer server(0,
		    server_ops, server_threads,
		    client_info_f, client_response_f);

  for (int i = 0; i < client_count; ++i) {
    clients[i] = new TestClient(i,
				std::bind(&TestServer::post, &server, _1, _2),
				client_goals[i] * goal_secs_to_run,
				client_goals[i],
				client_outstanding_ops);
  }

  // clients are now running

  for (int i = 0; i < client_count; ++i) {
    clients[i]->waitUntilDone();
  }

  const TestClient::TimePoint late_time = TestClient::now();
  TestClient::TimePoint latest_start = early_time;
  TestClient::TimePoint earliest_finish = late_time;
  TestClient::TimePoint latest_finish = early_time;
  
  // all clients are done
  for (int c = 0; c < client_count; ++c) {
    auto start = clients[c]->getOpTimes().front();
    auto end = clients[c]->getOpTimes().back();

    if (start > latest_start) { latest_start = start; }
    if (end < earliest_finish) { earliest_finish = end; }
    if (end > latest_finish) { latest_finish = end; }
  }

  const auto start_edge = latest_start + skip_amount;

  for (int c = 0; c < client_count; ++c) {
    auto it = clients[c]->getOpTimes().begin();
    const auto end = clients[c]->getOpTimes().end();
    while (it != end && *it < start_edge) { ++it; }

    for (auto time_edge = start_edge + measure_unit;
	 time_edge < latest_finish;
	 time_edge += measure_unit) {
      int count = 0;
      for (; it != end && *it < time_edge; ++count, ++it) { /* empty */ }
      double ops_per_second = double(count) / (measure_unit / report_unit);
      std::cout << "client " << c << ": " << ops_per_second << 
	" ops per second." << std::endl;
    }
  }

  // clean up

  for (int c = 0; c < client_count; ++c) {
    delete clients[c];
  }
  delete[] clients;

  std::cout << "simulation complete" << std::endl;
}
