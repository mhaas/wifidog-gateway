/* vim: set et sw=4 ts=4 sts=4 : */
/********************************************************************\
 * This program is free software; you can redistribute it and/or    *
 * modify it under the terms of the GNU General Public License as   *
 * published by the Free Software Foundation; either version 2 of   *
 * the License, or (at your option) any later version.              *
 *                                                                  *
 * This program is distributed in the hope that it will be useful,  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of   *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    *
 * GNU General Public License for more details.                     *
 *                                                                  *
 * You should have received a copy of the GNU General Public License*
 * along with this program; if not, contact:                        *
 *                                                                  *
 * Free Software Foundation           Voice:  +1-617-542-5942       *
 * 59 Temple Place - Suite 330        Fax:    +1-617-542-2652       *
 * Boston, MA  02111-1307,  USA       gnu@gnu.org                   *
 *                                                                  *
 \********************************************************************/

/** @internal
  @file firewall.c
  @brief Firewall update functions
  @author Copyright (C) 2004 Philippe April <papril777@yahoo.com>
  2006 Benoit Grégoire, Technologies Coeus inc. <bock@step.polymtl.ca>
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <errno.h>
#include <pthread.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/unistd.h>

#include <string.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/time.h>

#include <net/ethernet.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netpacket/packet.h>

#include "httpd.h"
#include "safe.h"
#include "debug.h"
#include "conf.h"
#include "firewall.h"
#include "fw_iptables.h"
#include "auth.h"
#include "centralserver.h"
#include "client_list.h"
#include "commandline.h"

/**
 * Allow a client access through the firewall by adding a rule in the firewall to MARK the user's packets with the proper
 * rule by providing his IP and MAC address
 * @param ip IP address to allow
 * @param mac MAC address to allow
 * @param fw_connection_state fw_connection_state Tag
 * @return Return code of the command
 */
int
fw_allow(const char *ip, const char *mac, int fw_connection_state)
{
    debug(LOG_DEBUG, "Allowing %s %s with fw_connection_state %d", ip, mac, fw_connection_state);

    return iptables_fw_access(FW_ACCESS_ALLOW, ip, mac, fw_connection_state);
}

/**
 * Allow a host through the firewall by adding a rule in the firewall
 * @param host IP address, domain or hostname to allow
 * @return Return code of the command
 */
int
fw_allow_host(const char *host)
{
    debug(LOG_DEBUG, "Allowing %s", host);

    return iptables_fw_access_host(FW_ACCESS_ALLOW, host);
}

/**
 * @brief Deny a client access through the firewall by removing the rule in the firewall that was fw_connection_stateging the user's traffic
 * @param ip IP address to deny
 * @param mac MAC address to deny
 * @param fw_connection_state fw_connection_state Tag
 * @return Return code of the command
 */
int
fw_deny(const char *ip, const char *mac, int fw_connection_state)
{
    debug(LOG_DEBUG, "Denying %s %s with fw_connection_state %d", ip, mac, fw_connection_state);

    return iptables_fw_access(FW_ACCESS_DENY, ip, mac, fw_connection_state);
}

/** Passthrough for clients when auth server is down */
int
fw_set_authdown(void)
{
	debug(LOG_DEBUG, "Marking auth server down");

	return iptables_fw_auth_unreachable(FW_MARK_AUTH_IS_DOWN);
}

/** Remove passthrough for clients when auth server is up */
int
fw_set_authup(void)
{
	debug(LOG_DEBUG, "Marking auth server up again");

	return iptables_fw_auth_reachable();
}



/* XXX DCY */
/**
 * Get an IP's MAC address from the ARP cache.
 * Go through all the entries in config->arp_table_path until we find the
 * requested IP address and return the MAC address bound to it.
 * @todo Make this function portable (using shell scripts?)
 */
char           *
arp_get(const char *req_ip)
{
    FILE           *proc;
	char ip[16];
	char mac[18];
	char * reply;
    s_config *config = config_get_config();

    if (!(proc = fopen(config->arp_table_path, "r"))) {
        return NULL;
    }

    /* Skip first line */
	while (!feof(proc) && fgetc(proc) != '\n');

	/* Find ip, copy mac in reply */
	reply = NULL;
    while (!feof(proc) && (fscanf(proc, " %15[0-9.] %*s %*s %17[A-Fa-f0-9:] %*s %*s", ip, mac) == 2)) {
		if (strcmp(ip, req_ip) == 0) {
			reply = safe_strdup(mac);
			break;
		}
    }

    fclose(proc);

    return reply;
}

/** Initialize the firewall rules
 */
int
fw_init(void)
{
    int flags, oneopt = 1, zeroopt = 0;
	int result = 0;
	t_client * client = NULL;

    debug(LOG_INFO, "Creating ICMP socket");
    if ((icmp_fd = socket (AF_INET, SOCK_RAW, IPPROTO_ICMP)) == -1 ||
            (flags = fcntl(icmp_fd, F_GETFL, 0)) == -1 ||
             fcntl(icmp_fd, F_SETFL, flags | O_NONBLOCK) == -1 ||
             setsockopt(icmp_fd, SOL_SOCKET, SO_RCVBUF, &oneopt, sizeof(oneopt)) ||
             setsockopt(icmp_fd, SOL_SOCKET, SO_DONTROUTE, &zeroopt, sizeof(zeroopt)) == -1) {
        debug(LOG_ERR, "Cannot create ICMP raw socket.");
        return 0;
    }

    debug(LOG_INFO, "Initializing Firewall");
    result = iptables_fw_init();

	if (restart_orig_pid) {
		debug(LOG_INFO, "Restoring firewall rules for clients inherited from parent");
		LOCK_CLIENT_LIST();
		client = client_get_first_client();
		while (client) {
		    fw_allow(client->ip, client->mac, client->fw_connection_state);
			client = client->next;
		}
		UNLOCK_CLIENT_LIST();
	}

	return result;
}

/** Remove all auth server firewall whitelist rules
 */
void
fw_clear_authservers(void)
{
	debug(LOG_INFO, "Clearing the authservers list");
	iptables_fw_clear_authservers();
}

/** Add the necessary firewall rules to whitelist the authservers
 */
void
fw_set_authservers(void)
{
	debug(LOG_INFO, "Setting the authservers list");
	iptables_fw_set_authservers();
}

/** Remove the firewall rules
 * This is used when we do a clean shutdown of WiFiDog.
 * @return Return code of the fw.destroy script
 */
int
fw_destroy(void)
{
    if (icmp_fd != 0) {
        debug(LOG_INFO, "Closing ICMP socket");
        close(icmp_fd);
    }

    debug(LOG_INFO, "Removing Firewall rules");
    return iptables_fw_destroy();
}

/**Probably a misnomer, this function actually refreshes the entire client list's traffic counter, re-authenticates every client with the central server and update's the central servers traffic counters and notifies it if a client has logged-out.
 * @todo Make this function smaller and use sub-fonctions
 */
void
fw_sync_with_authserver(void)
{
    t_authresponse  authresponse;
    char            *token, *ip, *mac;
    t_client        **clientlist = NULL;
    unsigned long long	    incoming, outgoing;
    int numclients;
    s_config *config = config_get_config();

    if (-1 == iptables_fw_counters_update()) {
        debug(LOG_ERR, "Could not get counters from firewall!");
        return;
    }

    LOCK_CLIENT_LIST();

    numclients = client_list_as_array(&clientlist);


    /* XXX Ideally, from a thread safety PoV, this function should build a list of client pointers,
     * iterate over the list and have an explicit "client still valid" check while list is locked.
     * That way clients can disappear during the cycle with no risk of trashing the heap or getting
     * a SIGSEGV.
     */
    debug(LOG_DEBUG, "numClients %d", numclients);
    for (int ii = 0; ii < numclients; ii++) {
    
        debug(LOG_DEBUG, "%d", ii);
        t_client * curclient = clientlist[ii];
        if (! client_still_valid(curclient)) {
            debug(LOG_ERR, "Node was freed while being re-validated!");
            continue; /* next please */
        }

        ip = safe_strdup(curclient->ip);
        token = safe_strdup(curclient->token);
        mac = safe_strdup(curclient->mac);
        outgoing = curclient->counters.outgoing;
        incoming = curclient->counters.incoming;

        debug(LOG_DEBUG, "fw_sync_with_authserver: ip %s, mac %s, token %s", ip, mac, token);
        debug(LOG_DEBUG, "fw_sync_with_authserver: about to unlock client list");

        // TODO: the problem is here, we might get a manual logout 
        UNLOCK_CLIENT_LIST();
        /* Ping the client, if he responds it'll keep activity on the link.
         * However, if the firewall blocks it, it will not help.  The suggested
         * way to deal witht his is to keep the DHCP lease time extremely
         * short:  Shorter than config->checkinterval * config->clienttimeout */
        debug(LOG_DEBUG, "fw_sync_with_authserver: ping ip %s, mac %s, token %s", ip);
        icmp_ping(ip);
        /* Update the counters on the remote server only if we have an auth server */
        if (config->auth_servers != NULL) {
            debug(LOG_DEBUG, "fw_sync_with_authserver: updating counters: ip %s, mac %s, token %s", ip);
            auth_server_request(&authresponse, REQUEST_TYPE_COUNTERS, ip, mac, token, incoming, outgoing);
        }
	    LOCK_CLIENT_LIST();

        if (!(client_still_valid(curclient))) {
            debug(LOG_ERR, "Node %s was freed while being re-validated!", ip);
        } else {
        	time_t	current_time=time(NULL);
        	debug(LOG_INFO, "Checking client %s for timeout:  Last updated %ld (%ld seconds ago), timeout delay %ld seconds, current time %ld, ",
                        curclient->ip, curclient->counters.last_updated, current_time-curclient->counters.last_updated, config->checkinterval * config->clienttimeout, current_time);
            if (curclient->counters.last_updated +
				(config->checkinterval * config->clienttimeout)
				<= current_time) {
                /* Timing out user */
                debug(LOG_INFO, "%s - Inactive for more than %ld seconds, removing client and denying in firewall",
                        curclient->ip, config->checkinterval * config->clienttimeout);
                logout_client(curclient);
            } else {
                /*
                 * This handles any change in
                 * the status this allows us
                 * to change the status of a
                 * user while he's connected
                 *
                 * Only run if we have an auth server
                 * configured!
                 */
                if (config->auth_servers != NULL) {
                    switch (authresponse.authcode) {
                        case AUTH_DENIED:
                            debug(LOG_NOTICE, "%s - Denied. Removing client and firewall rules", curclient->ip);
                            fw_deny(curclient->ip, curclient->mac, curclient->fw_connection_state);
                            client_list_delete(curclient);
                            break;

                        case AUTH_VALIDATION_FAILED:
                            debug(LOG_NOTICE, "%s - Validation timeout, now denied. Removing client and firewall rules", curclient->ip);
                            fw_deny(curclient->ip, curclient->mac, curclient->fw_connection_state);
                            client_list_delete(curclient);
                            break;

                        case AUTH_ALLOWED:
                            if (curclient->fw_connection_state != FW_MARK_KNOWN) {
                                debug(LOG_INFO, "%s - Access has changed to allowed, refreshing firewall and clearing counters", curclient->ip);
                                //WHY did we deny, then allow!?!? benoitg 2007-06-21
                                //fw_deny(p1->ip, p1->mac, p1->fw_connection_state); /* XXX this was possibly to avoid dupes. */

                                if (curclient->fw_connection_state != FW_MARK_PROBATION) {
                                    curclient->counters.incoming = curclient->counters.outgoing = 0;
                                }
                                else {
                                	//We don't want to clear counters if the user was in validation, it probably already transmitted data..
                                    debug(LOG_INFO, "%s - Skipped clearing counters after all, the user was previously in validation", curclient->ip);
                                }
                                curclient->fw_connection_state = FW_MARK_KNOWN;
                                fw_allow(curclient->ip, curclient->mac, curclient->fw_connection_state);
                            }
                            break;

                        case AUTH_VALIDATION:
                            /*
                             * Do nothing, user
                             * is in validation
                             * period
                             */
                            debug(LOG_INFO, "%s - User in validation period", curclient->ip);
                            break;

                        case AUTH_ERROR:
                            debug(LOG_WARNING, "Error communicating with auth server - leaving %s as-is for now", curclient->ip);
                            break;

                        default:
                            debug(LOG_ERR, "I do not know about authentication code %d", authresponse.authcode);
                            break;
                    }
                }
            }
        }

        free(token);
        free(ip);
        free(mac);
    }
    UNLOCK_CLIENT_LIST();
}

/**
 * @brief Logout a client and report to auth server.
 *
 * This function assumes it is being called with the client lock held! This
 * function remove the client from the client list and free its memory, so
 * client is no langer valid when this method returns.
 *
 * @param client Points to the client to be logged out
 */
void
logout_client(t_client *client)
{
    t_authresponse  authresponse;
    const s_config *config = config_get_config();
    fw_deny(client->ip, client->mac, client->fw_connection_state);
    client_list_remove(client);

    /* Advertise the logout if we have an auth server */
    if (config->auth_servers != NULL) {
        UNLOCK_CLIENT_LIST();
        auth_server_request(&authresponse, REQUEST_TYPE_LOGOUT,
            client->ip, client->mac, client->token,
            client->counters.incoming,
            client->counters.outgoing);

        if (authresponse.authcode==AUTH_ERROR)
            debug(LOG_WARNING, "Auth server error when reporting logout");
        LOCK_CLIENT_LIST();
    }

    client_free_node(client);
}

void
icmp_ping(const char *host)
{
	struct sockaddr_in saddr;
	struct {
		struct ip ip;
		struct icmp icmp;
	} packet;
	unsigned int i, j;
	int opt = 2000;
	unsigned short id = rand16();

	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	inet_aton(host, &saddr.sin_addr);
#if defined(HAVE_SOCKADDR_SA_LEN)
	saddr.sin_len = sizeof(struct sockaddr_in);
#endif

	memset(&packet.icmp, 0, sizeof(packet.icmp));
	packet.icmp.icmp_type = ICMP_ECHO;
	packet.icmp.icmp_id = id;

	for (j = 0, i = 0; i < sizeof(struct icmp) / 2; i++)
		j += ((unsigned short *)&packet.icmp)[i];

	while (j >> 16)
		j = (j & 0xffff) + (j >> 16);

	packet.icmp.icmp_cksum = (j == 0xffff) ? j : ~j;

	if (setsockopt(icmp_fd, SOL_SOCKET, SO_RCVBUF, &opt, sizeof(opt)) == -1)
		debug(LOG_ERR, "setsockopt(): %s", strerror(errno));

	if (sendto(icmp_fd, (char *)&packet.icmp, sizeof(struct icmp), 0,
	           (const struct sockaddr *)&saddr, sizeof(saddr)) == -1)
		debug(LOG_ERR, "sendto(): %s", strerror(errno));

	opt = 1;
	if (setsockopt(icmp_fd, SOL_SOCKET, SO_RCVBUF, &opt, sizeof(opt)) == -1)
		debug(LOG_ERR, "setsockopt(): %s", strerror(errno));

	return;
}

unsigned short rand16(void) {
  static int been_seeded = 0;

  if (!been_seeded) {
    unsigned int seed = 0;
    struct timeval now;

    /* not a very good seed but what the heck, it needs to be quickly acquired */
    gettimeofday(&now, NULL);
    seed = now.tv_sec ^ now.tv_usec ^ (getpid() << 16);

    srand(seed);
    been_seeded = 1;
    }

    /* Some rand() implementations have less randomness in low bits
     * than in high bits, so we only pay attention to the high ones.
     * But most implementations don't touch the high bit, so we
     * ignore that one.
     **/
      return( (unsigned short) (rand() >> 15) );
}
