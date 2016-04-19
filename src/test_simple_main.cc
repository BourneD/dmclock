// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

/*
 * Copyright (C) 2016 Red Hat Inc.
 */


#include "test_simple.h"


namespace test = test_simple;
namespace simp = crimson::simple_scheduler;

using namespace std::placeholders;


void server_data(std::ostream& out,
		 test::MySim* sim,
		 test::MySim::ServerFilter server_disp_filter,
		 int head_w, int data_w, int data_prec);


void client_data(std::ostream& out,
		 test::MySim* sim,
		 test::MySim::ClientFilter client_disp_filter,
		 int head_w, int data_w, int data_prec);


int main(int argc, char* argv[]) {
  // server params

  const uint server_count = 100;
  const uint server_iops = 40;
  const uint server_threads = 1;
  const bool server_soft_limit = false;

  // client params

  const uint client_total_ops = 1000;
  const uint client_count = 100;
  const uint client_wait_count = 1;
  const uint client_iops_goal = 50;
  const uint client_outstanding_ops = 100;
  const std::chrono::seconds client_wait(10);

  // client info
  
  const double client_reservation = 20.0;
  const double client_limit = 60.0;
  const double client_weight = 1.0;

#if 0 // REMOVE
  test::dmc::ClientInfo client_info =
      { client_weight, client_reservation, client_limit };

  auto client_info_f = [=](const ClientId& c) -> test::dmc::ClientInfo {
      return client_info;
  };

  test::dmc::ClientInfo test::client_info_f(const ClientId& c) {
      return client_info;
  }
#endif

  auto client_disp_filter = [=] (const ClientId& i) -> bool {
    return i < 3 || i >= (client_count - 3);
  };

  auto server_disp_filter = [=] (const ServerId& i) -> bool {
    return i < 3 || i >= (server_count - 3);
  };


  test::MySim *simulation;

  // lambda to post a request to the identified server; called by client
  test::SubmitFunc server_post_f =
    [&simulation](const ServerId& server_id,
		  const TestRequest& request,
		  const simp::ReqParams<ClientId>& req_params) {
    auto& server = simulation->get_server(server_id);
    server.post(request, req_params);
  };

  static std::vector<CliInst> no_wait =
    { { req_op, client_total_ops, client_iops_goal, client_outstanding_ops } };
  static std::vector<CliInst> wait =
    { { wait_op, client_wait },
      { req_op, client_total_ops, client_iops_goal, client_outstanding_ops } };

  simulation = new test::MySim();

  test::MySim::ClientBasedServerSelectFunc server_select_f =
    simulation->make_server_select_alt_range(8);

  test::SimpleServer::ClientRespFunc client_response_f =
    [&simulation](ClientId client_id,
		  const TestResponse& resp,
		  const simp::RespParams<ServerId>& resp_params) {
    simulation->get_client(client_id).receive_response(resp, resp_params);
  };

  test::CreateQueueF create_queue_f =
      [&](test::SimpleQueue::CanHandleRequestFunc can_f,
          test::SimpleQueue::HandleRequestFunc handle_f) -> test::SimpleQueue* {
    return new test::SimpleQueue(can_f, handle_f);
  };

  auto create_server_f = [&](ServerId id) -> test::SimpleServer* {
    return new test::SimpleServer(id,
				  server_iops, server_threads,
				  client_response_f,
				  test::simple_server_accumulate_f,
				  create_queue_f);
  };

  auto create_client_f = [&](ClientId id) -> test::SimpleClient* {
    return new test::SimpleClient(id,
				  server_post_f,
				  std::bind(server_select_f, _1, id),
				  test::simple_client_accumulate_f,
				  id < (client_count - client_wait_count)
				  ? no_wait : wait);
  };

  simulation->add_servers(server_count, create_server_f);
  simulation->add_clients(client_count, create_client_f);

  simulation->run();
  simulation->display_stats(std::cout,
			    &server_data, &client_data,
			    server_disp_filter, client_disp_filter);
} // main


void client_data(std::ostream& out,
		 test::MySim* sim,
		 test::MySim::ClientFilter client_disp_filter,
		 int head_w, int data_w, int data_prec) {
#if 0 // REMOVE
  // report how many ops were done by reservation and proportion for
  // each client

  int total_r = 0;
  out << std::setw(head_w) << "res_ops:";
  for (uint i = 0; i < sim->get_client_count(); ++i) {
    const auto& client = sim->get_client(i);
    auto r = client.get_accumulator().reservation_count;
    total_r += r;
    if (!client_disp_filter(i)) continue;
    out << std::setw(data_w) << r;
  }
  out << std::setw(data_w) << std::setprecision(data_prec) <<
    std::fixed << total_r << std::endl;

  int total_p = 0;
  out << std::setw(head_w) << "prop_ops:";
  for (uint i = 0; i < sim->get_client_count(); ++i) {
    const auto& client = sim->get_client(i);
    auto p = client.get_accumulator().proportion_count;
    total_p += p;
    if (!client_disp_filter(i)) continue;
    out << std::setw(data_w) << p;
  }
  out << std::setw(data_w) << std::setprecision(data_prec) <<
    std::fixed << total_p << std::endl;
#endif
}


void server_data(std::ostream& out,
		 test::MySim* sim,
		 test::MySim::ServerFilter server_disp_filter,
		 int head_w, int data_w, int data_prec) {
#if 0 // REMOVE
  out << std::setw(head_w) << "res_ops:";
  int total_r = 0;
  for (uint i = 0; i < sim->get_server_count(); ++i) {
    const auto& server = sim->get_server(i);
    auto rc = server.get_accumulator().reservation_count;
    total_r += rc;
    if (!server_disp_filter(i)) continue;
    out << std::setw(data_w) << rc;
  }
  out << std::setw(data_w) << std::setprecision(data_prec) <<
    std::fixed << total_r << std::endl;

  out << std::setw(head_w) << "prop_ops:";
  int total_p = 0;
  for (uint i = 0; i < sim->get_server_count(); ++i) {
    const auto& server = sim->get_server(i);
    auto pc = server.get_accumulator().proportion_count;
    total_p += pc;
    if (!server_disp_filter(i)) continue;
    out << std::setw(data_w) << pc;
  }
  out << std::setw(data_w) << std::setprecision(data_prec) <<
    std::fixed << total_p << std::endl;
#endif
}
