// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

/*
 * Copyright (C) 2016 Red Hat Inc.
 */



#include "dmclock_recs.h"

#include "test_simp_recs.h"
#include "test_simp_server.h"
#include "test_simp_client.h"
#include "simulate_common.h"
#include "simulate.h"


using namespace std::placeholders;

namespace dmc = crimson::dmclock;

using DmcServerAddInfo = crimson::dmclock::PhaseType;


struct DmcAccum {
  uint64_t reservation_count = 0;
  uint64_t proportion_count = 0;
};



void dmc_server_accumulate_f(DmcAccum& a, const DmcServerAddInfo& add_info) {
  if (dmc::PhaseType::reservation == add_info) {
    ++a.reservation_count;
  } else {
    ++a.proportion_count;
  }
}


void dmc_client_accumulate_f(DmcAccum& a, const dmc::RespParams<ServerId>& r) {
  if (dmc::PhaseType::reservation == r.phase) {
    ++a.reservation_count;
  } else {
    ++a.proportion_count;
  }
}


#if 0 // do last
void client_data(std::ostream& out, clients, int head_w, int data_w) {
  // report how many ops were done by reservation and proportion for
  // each client

  {
    std::cout << std::setw(head_w) << "res_ops:";
    int total = 0;
    for (auto const &c : clients) {
      auto r = c.second->get_accumulator().reservation_count;
      total += r;
      if (!client_disp_filter(c.first)) continue;
      std::cout << std::setw(data_w) << r;
    }
    std::cout << std::setw(data_w) << std::setprecision(data_prec) <<
      std::fixed << total << std::endl;
  }

  {
    std::cout << std::setw(head_w) << "prop_ops:";
    int total = 0;
    for (auto const &c : clients) {
      auto p = c.second->get_accumulator().proportion_count;
      total += p;
      if (!client_disp_filter(c.first)) continue;
      std::cout << std::setw(data_w) << p;
    }
    std::cout << std::setw(data_w) << std::setprecision(data_prec) <<
      std::fixed << total << std::endl;
  }
}


void server_data(std::ostream& out, servers, int head_w, int data_w) {
  {
    std::cout << std::setw(head_w) << "res_ops:";
    int total = 0;
    for (auto const &s : servers) {
      auto rc = s.second->get_accumulator().reservation_count;
      total += rc;
      if (!server_disp_filter(s.first)) continue;
      std::cout << std::setw(data_w) << rc;
    }
    std::cout << std::setw(data_w) << std::setprecision(data_prec) <<
      std::fixed << total << std::endl;
  }

  {
    std::cout << std::setw(head_w) << "prop_ops:";
    int total = 0;
    for (auto const &s : servers) {
      auto pc = s.second->get_accumulator().proportion_count;
      total += pc;
      if (!server_disp_filter(s.first)) continue;
      std::cout << std::setw(data_w) << pc;
    }
    std::cout << std::setw(data_w) << std::setprecision(data_prec) <<
      std::fixed << total << std::endl;
  }
}
#endif // do last

  const double client_reservation = 20.0;
  const double client_limit = 60.0;
  const double client_weight = 1.0;

  dmc::ClientInfo client_info =
    { client_weight, client_reservation, client_limit };

  // construct servers

#if 0 // REMOVE
  auto client_info_f = [&client_info](const ClientId& c) -> dmc::ClientInfo {
    return client_info;
  };
#endif


dmc::ClientInfo client_info_f(const ClientId& c) {
  return client_info;
}

using DmcServer = TestServer<dmc::PriorityQueue<ClientId,TestRequest>,
                      dmc::ClientInfo,
                      dmc::ReqParams<ClientId>,
                      dmc::RespParams<ServerId>,
                      DmcServerAddInfo,
                      DmcAccum>;

using DmcClient = TestClient<dmc::ServiceTracker<ServerId>,
                      dmc::ReqParams<ClientId>,
                      dmc::RespParams<ServerId>,
		      DmcAccum>;

using SelectFunc = Dmc::ServerSelectFunc;
using SubmitFunc = Dmc::SubmitFunc;


DmcServer::ClientRespFunc client_response_f =
    [&clients](ClientId client_id,
	       const TestResponse& resp,
	       const dmc::RespParams<ServerId>& resp_params) {
    clients[client_id]->receive_response(resp, resp_params);
  };


int main(int argc, char* argv[]) {
  simulate<DmcServer,DmcClient,dmc::ClientInfo>(client_info_f);
}
