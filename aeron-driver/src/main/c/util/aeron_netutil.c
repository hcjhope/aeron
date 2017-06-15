/*
 * Copyright 2014 - 2017 Real Logic Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#if defined(__linux__)
#define _GNU_SOURCE
#endif

#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <regex.h>
#include <ifaddrs.h>
#include <net/if.h>
#include "util/aeron_netutil.h"
#include "util/aeron_error.h"

static aeron_uri_hostname_resolver_func_t aeron_uri_hostname_resolver_func = NULL;
static void *aeron_uri_hostname_resolver_clientd = NULL;

void aeron_uri_hostname_resolver(aeron_uri_hostname_resolver_func_t func, void *clientd)
{
    aeron_uri_hostname_resolver_func = func;
    aeron_uri_hostname_resolver_clientd = clientd;
}

int aeron_ip_addr_resolver(const char *host, struct sockaddr_storage *sockaddr, int family_hint)
{
    struct addrinfo hints;
    struct addrinfo *info = NULL;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = family_hint;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    int error, result = -1;
    if ((error = getaddrinfo(host, NULL, &hints, &info)) != 0)
    {
        if (NULL == aeron_uri_hostname_resolver_func)
        {
            aeron_set_err(EINVAL, "Unable to resolve host=(%s): (%d) %s", host, error, gai_strerror(error));
            return -1;
        }
        else if (aeron_uri_hostname_resolver_func(aeron_uri_hostname_resolver_clientd, host, &hints, &info) != 0)
        {
            aeron_set_err(EINVAL, "Unable to resolve host=(%s): %s", host, aeron_errmsg());
            return -1;
        }
    }

    if (info->ai_family == AF_INET)
    {
        memcpy(sockaddr, &info->ai_addr, sizeof(struct sockaddr_in));
        sockaddr->ss_family = AF_INET;
        result = 0;
    }
    else if (info->ai_family == AF_INET6)
    {
        memcpy(sockaddr, &info->ai_addr, sizeof(struct sockaddr_in6));
        sockaddr->ss_family = AF_INET6;
        result = 0;
    }
    else
    {
        aeron_set_err(EINVAL, "Only IPv4 and IPv6 hosts are supported: family=%d", info->ai_family);
    }

    freeaddrinfo(info);
    return result;
}

int aeron_ipv4_addr_resolver(const char *host, struct sockaddr_storage *sockaddr)
{
    struct sockaddr_in *addr = (struct sockaddr_in *)sockaddr;

    if (inet_pton(AF_INET, host, &addr->sin_addr))
    {
        sockaddr->ss_family = AF_INET;
        return 0;
    }

    return aeron_ip_addr_resolver(host, sockaddr, AF_INET);
}

int aeron_ipv6_addr_resolver(const char *host, struct sockaddr_storage *sockaddr)
{
    struct sockaddr_in6 *addr = (struct sockaddr_in6 *)sockaddr;

    if (inet_pton(AF_INET6, host, &addr->sin6_addr))
    {
        sockaddr->ss_family = AF_INET6;
        return 0;
    }

    return aeron_ip_addr_resolver(host, sockaddr, AF_INET6);
}

int aeron_udp_port_resolver(const char *port_str)
{
    if (':' == *port_str)
    {
        port_str++;
    }

    unsigned long value = strtoul(port_str, NULL, 0);

    if (0 == value && EINVAL == errno)
    {
        aeron_set_err(EINVAL, "port invalid: %s", port_str);
        return -1;
    }
    else if (value >= UINT16_MAX)
    {
        aeron_set_err(EINVAL, "port out of range: %s", port_str);
        return -1;
    }

    return (int)value;
}

int aeron_host_and_port_resolver(
    const char *host_str, const char *port_str, struct sockaddr_storage *sockaddr, int family_hint)
{
    int result = -1, port = aeron_udp_port_resolver(port_str);

    if (port >= 0)
    {
        if (AF_INET == family_hint)
        {
            ((struct sockaddr_in *) sockaddr)->sin_port = htons(port);
            result = aeron_ipv4_addr_resolver(host_str, sockaddr);
        }
        else if (AF_INET6 == family_hint)
        {
            ((struct sockaddr_in6 *) sockaddr)->sin6_port = htons(port);
            result = aeron_ipv6_addr_resolver(host_str, sockaddr);
        }
    }

    return result;
}

#if defined(Darwin)
#define AERON_IPV4_REGCOMP_CFLAGS (REG_EXTENDED)
#else
#define AERON_IPV4_REGCOMP_CFLAGS (REG_EXTENDED)
#endif

int aeron_host_and_port_parse_and_resolve(const char *address_str, struct sockaddr_storage *sockaddr)
{
    static bool regexs_compiled = false;
    static regex_t ipv4_regex, ipv6_regex;
    regmatch_t matches[8];

    if (!regexs_compiled)
    {
        const char *ipv4 = "([^:]+)(:([0-9]+))?";
        const char *ipv6 = "\\[([0-9A-Fa-f:]+)(%([a-zA-Z0-9_.~-]+))?\\](:([0-9]+))?";

        int regcomp_result;
        if ((regcomp_result = regcomp(&ipv4_regex, ipv4, AERON_IPV4_REGCOMP_CFLAGS)) != 0)
        {
            char message[AERON_MAX_PATH];

            regerror(regcomp_result, &ipv4_regex, message, sizeof(message));
            aeron_set_err(EINVAL, "could not regcomp IPv4 regex: %s", message);
            return -1;
        }

        if ((regcomp_result = regcomp(&ipv6_regex, ipv6, AERON_IPV4_REGCOMP_CFLAGS)) != 0)
        {
            char message[AERON_MAX_PATH];

            regerror(regcomp_result, &ipv6_regex, message, sizeof(message));
            aeron_set_err(EINVAL, "could not regcomp IPv6 regex: %s", message);
            return -1;
        }

        regexs_compiled = true;
    }

    int regexec_result = regexec(&ipv6_regex, address_str, 8, matches, 0);
    if (0 == regexec_result)
    {
        char host[AERON_MAX_PATH], port[AERON_MAX_PATH];
        size_t host_len = (size_t)(matches[1].rm_eo - matches[1].rm_so);
        size_t port_len = (size_t)(matches[4].rm_eo - matches[4].rm_so);

        strncpy(host, &address_str[matches[1].rm_so], host_len);
        host[host_len] = '\0';
        strncpy(port, &address_str[matches[4].rm_so], port_len);
        port[port_len] = '\0';

        return aeron_host_and_port_resolver(host, port, sockaddr, AF_INET6);
    }
    else if (REG_NOMATCH != regexec_result)
    {
        char message[AERON_MAX_PATH];

        regerror(regexec_result, &ipv4_regex, message, sizeof(message));
        aeron_set_err(EINVAL, "could not regexec IPv6 regex: %s", message);
        return -1;
    }

    regexec_result = regexec(&ipv4_regex, address_str, 3, matches, 0);
    if (0 == regexec_result)
    {
        char host[AERON_MAX_PATH], port[AERON_MAX_PATH];
        size_t host_len = (size_t)(matches[1].rm_eo - matches[1].rm_so);
        size_t port_len = (size_t)(matches[2].rm_eo - matches[2].rm_so);

        strncpy(host, &address_str[matches[1].rm_so], host_len);
        host[host_len] = '\0';
        strncpy(port, &address_str[matches[2].rm_so], port_len);
        port[port_len] = '\0';

        return aeron_host_and_port_resolver(host, port, sockaddr, AF_INET);
    }
    else if (REG_NOMATCH != regexec_result)
    {
        char message[AERON_MAX_PATH];

        regerror(regexec_result, &ipv4_regex, message, sizeof(message));
        aeron_set_err(EINVAL, "could not regexec IPv4 regex: %s", message);
        return -1;
    }

    aeron_set_err(EINVAL, "invalid format: %s", address_str);
    return -1;
}

int aeron_prefixlen_resolver(const char *prefixlen, unsigned long max)
{
    if ('\0' == *prefixlen)
    {
        return (int)max;
    }

    if ('/' == *prefixlen)
    {
        prefixlen++;
    }

    if (strcmp("0", prefixlen) == 0)
    {
        return 0;
    }

    unsigned long value = strtoul(prefixlen, NULL, 0);

    if (0 == value && EINVAL == errno)
    {
        aeron_set_err(EINVAL, "prefixlen invalid: %s", prefixlen);
        return -1;
    }
    else if (value > max)
    {
        aeron_set_err(EINVAL, "prefixlen out of range: %s", prefixlen);
        return -1;
    }

    return (int)value;
}

int aeron_host_and_prefixlen_resolver(
    const char *host_str, const char *prefixlen_str, struct sockaddr_storage *sockaddr, size_t *prefixlen, int family_hint)
{
    int host_result = -1, prefixlen_result = -1;

    if (AF_INET == family_hint)
    {
        host_result = aeron_ipv4_addr_resolver(host_str, sockaddr);
    }
    else if (AF_INET6 == family_hint)
    {
        host_result = aeron_ipv6_addr_resolver(host_str, sockaddr);
    }

    if (host_result >= 0)
    {
        if ((prefixlen_result = aeron_prefixlen_resolver(prefixlen_str, (sockaddr->ss_family == AF_INET6) ? 128 : 32)) >= 0)
        {
            *prefixlen = (size_t)prefixlen_result;
        }
    }

    return prefixlen_result >= 0 ? 0 : prefixlen_result;
}

int aeron_interface_parse_and_resolve(const char *interface_str, struct sockaddr_storage *sockaddr, size_t *prefixlen)
{
    static bool regexs_compiled = false;
    static regex_t ipv4_regex, ipv6_regex;
    regmatch_t matches[10];

    if (!regexs_compiled)
    {
        const char *ipv4 = "([^:/]+)(:([0-9]+))?(/([0-9]+))?";
        const char *ipv6 = "\\[([0-9A-Fa-f:]+)(%([a-zA-Z0-9_.~-]+))?\\](:([0-9]+))?(/([0-9]+))?";

        int regcomp_result;
        if ((regcomp_result = regcomp(&ipv4_regex, ipv4, AERON_IPV4_REGCOMP_CFLAGS)) != 0)
        {
            char message[AERON_MAX_PATH];

            regerror(regcomp_result, &ipv4_regex, message, sizeof(message));
            aeron_set_err(EINVAL, "could not regcomp IPv4 regex: %s", message);
            return -1;
        }

        if ((regcomp_result = regcomp(&ipv6_regex, ipv6, AERON_IPV4_REGCOMP_CFLAGS)) != 0)
        {
            char message[AERON_MAX_PATH];

            regerror(regcomp_result, &ipv6_regex, message, sizeof(message));
            aeron_set_err(EINVAL, "could not regcomp IPv6 regex: %s", message);
            return -1;
        }

        regexs_compiled = true;
    }

    int regexec_result = regexec(&ipv6_regex, interface_str, 10, matches, 0);
    if (0 == regexec_result)
    {
        char host_str[AERON_MAX_PATH], prefixlen_str[AERON_MAX_PATH];
        size_t host_str_len = (size_t)(matches[1].rm_eo - matches[1].rm_so);
        size_t prefixlen_str_len = (size_t)(matches[6].rm_eo - matches[6].rm_so);

        strncpy(host_str, &interface_str[matches[1].rm_so], host_str_len);
        host_str[host_str_len] = '\0';
        strncpy(prefixlen_str, &interface_str[matches[6].rm_so], prefixlen_str_len);
        prefixlen_str[prefixlen_str_len] = '\0';

        return aeron_host_and_prefixlen_resolver(host_str, prefixlen_str, sockaddr, prefixlen, AF_INET6);
    }
    else if (REG_NOMATCH != regexec_result)
    {
        char message[AERON_MAX_PATH];

        regerror(regexec_result, &ipv4_regex, message, sizeof(message));
        aeron_set_err(EINVAL, "could not regexec IPv6 regex: %s", message);
        return -1;
    }

    regexec_result = regexec(&ipv4_regex, interface_str, 5, matches, 0);
    if (0 == regexec_result)
    {
        char host_str[AERON_MAX_PATH], prefixlen_str[AERON_MAX_PATH];
        size_t host_str_len = (size_t)(matches[1].rm_eo - matches[1].rm_so);
        size_t prefixlen_str_len = (size_t)(matches[4].rm_eo - matches[4].rm_so);

        strncpy(host_str, &interface_str[matches[1].rm_so], host_str_len);
        host_str[host_str_len] = '\0';
        strncpy(prefixlen_str, &interface_str[matches[4].rm_so], prefixlen_str_len);
        prefixlen_str[prefixlen_str_len] = '\0';

        return aeron_host_and_prefixlen_resolver(host_str, prefixlen_str, sockaddr, prefixlen, AF_INET);
    }
    else if (REG_NOMATCH != regexec_result)
    {
        char message[AERON_MAX_PATH];

        regerror(regexec_result, &ipv4_regex, message, sizeof(message));
        aeron_set_err(EINVAL, "could not regexec IPv4 regex: %s", message);
        return -1;
    }

    aeron_set_err(EINVAL, "invalid format: %s", interface_str);
    return -1;
}

int aeron_lookup_ipv4_interfaces(aeron_ipv4_ifaddr_func_t func)
{
    struct ifaddrs *ifaddrs = NULL;
    int result = -1;

    if (getifaddrs(&ifaddrs) >= 0)
    {
        for (struct ifaddrs *ifa = ifaddrs;  ifa != NULL; ifa  = ifa->ifa_next)
        {
            if (NULL == ifa->ifa_addr)
            {
                continue;
            }

            if (AF_INET == ifa->ifa_addr->sa_family)
            {
                unsigned int interface_index = if_nametoindex(ifa->ifa_name);

                result++;
                func(interface_index, ifa->ifa_name, (struct sockaddr_in *)ifa->ifa_addr, (struct sockaddr_in *)ifa->ifa_netmask, ifa->ifa_flags);
            }
        }

        freeifaddrs(ifaddrs);
        return result;
    }

    return result;
}

int aeron_lookup_ipv6_interfaces(aeron_ipv6_ifaddr_func_t func)
{
    struct ifaddrs *ifaddrs = NULL;
    int result = -1;

    if (getifaddrs(&ifaddrs) >= 0)
    {
        for (struct ifaddrs *ifa = ifaddrs;  ifa != NULL; ifa  = ifa->ifa_next)
        {
            if (NULL == ifa->ifa_addr)
            {
                continue;
            }

            if (AF_INET6 == ifa->ifa_addr->sa_family)
            {
                unsigned int interface_index = if_nametoindex(ifa->ifa_name);

                result++;
                func(interface_index, ifa->ifa_name, (struct sockaddr_in6 *)ifa->ifa_addr, (struct sockaddr_in6 *)ifa->ifa_netmask, ifa->ifa_flags);
            }
        }

        freeifaddrs(ifaddrs);
        return result;
    }

    return result;
}

union _aeron_128b_as_64b
{
    __uint128_t value;
    uint64_t q[2];
};

void aeron_ipv6_netmask_from_prefixlen(struct in6_addr *addr, size_t prefixlen)
{
    union _aeron_128b_as_64b netmask;

    if (0 == prefixlen)
    {
        netmask.value = ~(-1);
    }
    else
    {
        netmask.value = ~(((__uint128_t)1 << (128 - prefixlen)) - 1);
    }

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    netmask.q[1] = __builtin_bswap64(netmask.q[0]);
    netmask.q[0] = __builtin_bswap64(netmask.q[1]);
#endif

    memcpy(addr, &netmask, sizeof(struct in6_addr));
}

bool aeron_ipv6_on_same_network(struct in6_addr *in6_addr1, struct in6_addr *in6_addr2, struct in6_addr *netmask_addr)
{
    __uint128_t addr1;
    __uint128_t addr2;
    __uint128_t netmask;

    memcpy(&addr1, in6_addr1, sizeof(addr1));
    memcpy(&addr2, in6_addr2, sizeof(addr2));
    memcpy(&netmask, netmask_addr, sizeof(netmask));

    return (addr1 & netmask) == (addr2 & netmask);
}
