// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

/*
 * Copyright (C) 2016 Red Hat Inc.
 */


#include "dmclock_recs.h"
#include "dmclock_server.h"
#include "dmclock_client.h"

#include "sim_recs.h"
#include "sim_server.h"
#include "sim_client.h"

#include "test_dmclock.h"


namespace test = test_dmc;


void test::dmc_server_accumulate_f(test::DmcAccum& a,
				   const test::DmcServerAddInfo& add_info) {
  if (test::dmc::PhaseType::reservation == add_info) {
    ++a.reservation_count;
  } else {
    ++a.proportion_count;
  }
}


void test::dmc_client_accumulate_f(test::DmcAccum& a,
				   const test::dmc::RespParams<ServerId>& r) {
  if (test::dmc::PhaseType::reservation == r.phase) {
    ++a.reservation_count;
  } else {
    ++a.proportion_count;
  }
}
