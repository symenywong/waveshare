#include "aiqa_management_access.h"

#include <assert.h>
#include <string.h>

static aiqa_management_security_context_t access_for(
    uint64_t connection,
    uint64_t authorization,
    uint64_t session)
{
    aiqa_management_security_context_t access = {
        .connection_generation = connection,
        .authorization_generation = authorization,
    };
    for (size_t index = 0; index < sizeof(access.session_id); ++index) {
        access.session_id[7U - index] = (uint8_t)(session >> (index * 8U));
    }
    return access;
}

static void run_activation(void)
{
    aiqa_management_access_registry_t registry;
    aiqa_management_access_registry_init(&registry);
    const aiqa_management_security_context_t access = access_for(3, 9, 0x0102030405060708ULL);

    assert(!aiqa_management_access_registry_authorize(&registry, &access));
    assert(aiqa_management_access_registry_prepare(&registry, &access));
    assert(!aiqa_management_access_registry_authorize(&registry, &access));
    assert(aiqa_management_access_registry_activate(&registry, 3, 9));
    assert(aiqa_management_access_registry_authorize(&registry, &access));
}

static void run_mismatch(void)
{
    aiqa_management_access_registry_t registry;
    aiqa_management_access_registry_init(&registry);
    const aiqa_management_security_context_t access = access_for(4, 10, 0x1112131415161718ULL);
    assert(aiqa_management_access_registry_prepare(&registry, &access));
    assert(aiqa_management_access_registry_activate(&registry, 4, 10));

    aiqa_management_security_context_t wrong = access;
    wrong.connection_generation += 1U;
    assert(!aiqa_management_access_registry_authorize(&registry, &wrong));
    wrong = access;
    wrong.authorization_generation += 1U;
    assert(!aiqa_management_access_registry_authorize(&registry, &wrong));
    wrong = access;
    wrong.session_id[0] ^= 1U;
    assert(!aiqa_management_access_registry_authorize(&registry, &wrong));
    assert(!aiqa_management_access_registry_activate(&registry, 4, 11));

    aiqa_management_security_context_t invalid = access_for(0, 1, 1);
    assert(!aiqa_management_access_registry_prepare(&registry, &invalid));
    invalid = access_for(1, 0, 1);
    assert(!aiqa_management_access_registry_prepare(&registry, &invalid));
    invalid = access_for(1, 1, 0);
    assert(!aiqa_management_access_registry_prepare(&registry, &invalid));
}

static void run_revoke(void)
{
    aiqa_management_access_registry_t registry;
    aiqa_management_access_registry_init(&registry);
    const aiqa_management_security_context_t first = access_for(5, 12, 0x2122232425262728ULL);
    assert(aiqa_management_access_registry_prepare(&registry, &first));
    assert(aiqa_management_access_registry_activate(&registry, 5, 12));
    assert(aiqa_management_access_registry_authorize(&registry, &first));
    aiqa_management_access_registry_revoke(&registry);
    assert(!aiqa_management_access_registry_authorize(&registry, &first));

    const aiqa_management_security_context_t second = access_for(6, 13, 0x3132333435363738ULL);
    assert(aiqa_management_access_registry_prepare(&registry, &second));
    assert(aiqa_management_access_registry_activate(&registry, 6, 13));
    assert(!aiqa_management_access_registry_authorize(&registry, &first));
    assert(aiqa_management_access_registry_authorize(&registry, &second));
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    if (strcmp(argv[1], "activation") == 0) {
        run_activation();
    } else if (strcmp(argv[1], "mismatch") == 0) {
        run_mismatch();
    } else if (strcmp(argv[1], "revoke") == 0) {
        run_revoke();
    } else {
        return 2;
    }
    return 0;
}
