/*************************************************************************
    > File Name: nwk.c
    > Author: Kevin
    > Created Time: 2019-12-23
    > Description:
 ************************************************************************/

/* Includes *********************************************************** */
#include <stdio.h>
#include <unistd.h>

#include "cli.h"
#include "host_gecko.h"
#include "nwk.h"
#include "mng.h"
#include "logging.h"
#include "gecko_bglib.h"
#include "bg_uart_cbs.h"
#include "socket_handler.h"
#include "generic_parser.h"
#include "startup.h"

/* Defines  *********************************************************** */

/* Global Variables *************************************************** */

/* Static Variables *************************************************** */

/* Static Functions Declaractions ************************************* */
static err_t on_initialized_config(struct gecko_msg_mesh_prov_initialized_evt_t *ein);

err_t nwk_init(void *p)
{
  uint16_t ret;
  struct gecko_cmd_packet *evt = NULL;
  err_t e = ec_success;

  mng_t *mng = get_mng();
  if (bg_err_success != (ret = gecko_cmd_mesh_prov_init()->result)) {
    LOGBGE("gecko_cmd_mesh_prov_init", ret);
    return err(ec_bgrsp);
  }

  while (NULL == evt || BGLIB_MSG_ID(evt->header) != gecko_evt_mesh_prov_initialized_id) {
    if (getprojargs()->enc) {
      poll_update(50);
    }
    evt = gecko_peek_event();
    /* Blocking wait for initialized event, timeout could be added to increase the robust */
    usleep(500);
  }

  mng->state = initialized;
  LOGM("NCP ---> NWK Initialized\n");
  EC(ec_success, on_initialized_config(&evt->data.evt_mesh_prov_initialized));
  mng->state = configured;
  mng_load_lists();     /* do the initial loading */
  LOGM("Network configured and nodes loaded\n");

  /*
   * Initialize all the required model classes
   */
  /* Generic client model */
  if (bg_err_success != (ret = gecko_cmd_mesh_generic_client_init()->result)) {
    LOGBGE("gecko_cmd_mesh_generic_client_init", ret);
    return err(ec_bgrsp);
  }
  /* Sensor client model */
  if (bg_err_success != (ret = gecko_cmd_mesh_sensor_client_init()->result)) {
    LOGBGE("gecko_cmd_mesh_sensor_client_init", ret);
    return err(ec_bgrsp);
  }
#if (LC_CLIENT_PRESENT == 1)
  /* LC client model */
  if (bg_err_success != (ret = gecko_cmd_mesh_lc_client_init(LC_ELEM_INDEX)->result)) {
    LOGBGE("gecko_cmd_mesh_lc_client_init", ret);
    return err(ec_bgrsp);
  }
#endif
#if (SCENE_CLIENT_PRESENT == 1)
  /* Scene client model */
  if (bg_err_success != (ret = gecko_cmd_mesh_scene_client_init(SCENE_ELEM_INDEX)->result)) {
    LOGBGE("gecko_cmd_mesh_scene_client_init", ret);
    return err(ec_bgrsp);
  }
#endif

  return e;
}

static err_t new_netkey(mng_t *mng)
{
  err_t e;
  struct gecko_msg_mesh_prov_create_network_rsp_t *rsp;

  if (mng->cfg->subnets[0].netkey.done) {
    return ec_success;
  }

  rsp = gecko_cmd_mesh_prov_create_network(16,
                                           mng->cfg->subnets[0].netkey.val);

  if (rsp->result == bg_err_success || rsp->result == bg_err_mesh_already_exists) {
    mng->cfg->subnets[0].netkey.id = rsp->network_id;
    mng->cfg->subnets[0].netkey.done = 1;

    EC(ec_success, provset_netkeyid(&mng->cfg->subnets[0].netkey.id));
    EC(ec_success, provset_netkeydone(&mng->cfg->subnets[0].netkey.done));
    return ec_success;
  } else if (rsp->result == bg_err_out_of_memory
             || rsp->result == bg_err_mesh_limit_reached) {
    LOGBGE("create netkey (max reach?)", rsp->result);
  } else {
    LOGBGE("create netkey", rsp->result);
  }
  return err(ec_bgrsp);
}

static err_t new_appkeys(mng_t *mng)
{
  err_t e;
  int tmp = 0;
  struct gecko_msg_mesh_prov_create_appkey_rsp_t *rsp;

  if (!mng->cfg->subnets[0].netkey.done) {
    LOGE("Must Create Network BEFORE Creating Appkeys.\n");
    return err(ec_state);
  }

  for (int i = 0; i < mng->cfg->subnets[0].appkey_num; i++) {
    meshkey_t *appkey = &mng->cfg->subnets[0].appkey[i];
    if (appkey->done) {
      tmp++;
      continue;
    }
    rsp = gecko_cmd_mesh_prov_create_appkey(mng->cfg->subnets[0].netkey.id,
                                            16,
                                            appkey->val);

    if (rsp->result == bg_err_success
        || rsp->result == bg_err_mesh_already_exists) {
      appkey->id = rsp->appkey_index;
      appkey->done = 1;
      EC(ec_success, provset_appkeyid(&appkey->refid, &appkey->id));
      EC(ec_success, provset_appkeydone(&appkey->refid, &appkey->done));
      tmp++;
    } else if (rsp->result == bg_err_out_of_memory
               || rsp->result == bg_err_mesh_limit_reached) {
      LOGBGE("create appkey (max reach?)", rsp->result);
    } else {
      LOGBGE("create appkey", rsp->result);
    }
  }
  mng->cfg->subnets[0].active_appkey_num = tmp;
  return ec_success;
}

static void self_config(const mng_t *mng)
{
  uint16_t ret;
  if (mng->cfg->net_txp) {
    if (bg_err_success != (ret = gecko_cmd_mesh_test_set_nettx(
                             mng->cfg->net_txp->cnt,
                             mng->cfg->net_txp->intv)->result)) {
      LOGBGE("Set local nettx", ret);
      return;
    }
    LOGM("Set local network transmission Count/Interval [%d/%dms] Success\n",
         mng->cfg->net_txp->cnt + 1,
         (mng->cfg->net_txp->intv + 1) * 10);
  }
  if (mng->cfg->timeout) {
    if (bg_err_success != (ret = gecko_cmd_mesh_config_client_set_default_timeout(
                             mng->cfg->timeout->normal,
                             mng->cfg->timeout->lpn)->result)) {
      LOGBGE("Set local timeouts", ret);
      return;
    }
    LOGM("Set config client timeout for normal node/LPN [%dms/%dms] Success\n",
         mng->cfg->timeout->normal,
         mng->cfg->timeout->lpn);
  }
}

static err_t on_initialized_config(struct gecko_msg_mesh_prov_initialized_evt_t *ein)
{
  err_t e;
  mng_t *mng = get_mng();

  if (ein->networks == 0 && (mng->cfg->addr || mng->cfg->ivi)) {
    uint16_t ret = gecko_cmd_mesh_prov_initialize_network(mng->cfg->addr,
                                                          mng->cfg->ivi)->result;
    if (!(bg_err_success == ret || bg_err_mesh_already_initialized == ret)) {
      LOGBGE("gecko_cmd_mesh_prov_initialize_network", ret);
      return err(ec_bgrsp);
    }
  } else if (ein->networks && (!mng->cfg->subnets || !mng->cfg->subnets[0].netkey.done)) {
    bt_shell_printf(COLOR_HIGHLIGHT
                    "**NETKEY MISMATCH**, NEED FACTORY RESET?"
                    COLOR_OFF
                    "\n");
    return err(ec_state);
  } else if (ein->networks) {
    if (ein->address != mng->cfg->addr) {
      mng->cfg->addr = ein->address;
      EC(ec_success, provset_addr(&mng->cfg->addr));
    }
    if (ein->ivi != mng->cfg->ivi) {
      mng->cfg->ivi = ein->ivi;
      EC(ec_success, provset_ivi(&mng->cfg->ivi));
    }
  }

  /* Network Keys */
  EC(ec_success, new_netkey(mng));
  /* App Keys */
  EC(ec_success, new_appkeys(mng));
  /* set nettx and timeouts */
  self_config(mng);

  return ec_success;
}

int bgevt_dflt_hdr(const struct gecko_cmd_packet *evt)
{
  switch (BGLIB_MSG_ID(evt->header)) {
    case gecko_evt_le_gap_adv_timeout_id:
      /* Ignore */
      break;
    default:
      return 0;
  }
  return 1;
}
