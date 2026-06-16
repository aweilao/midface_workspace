#include "MidSurface.h"
#include "config/MidSurfaceConfigLoader.hpp"

#include "license.hxx"
#include "spa_unlock_result.hxx"
#include "spa_unlock_state.h"
#include "ga_api.hxx"
#include "cstrapi.hxx"
#include "ofstapi.hxx"
#include "boolapi.hxx"
#include "kernapi.hxx"
#include "errorbase.hxx"

#include <cstdio>
#include <cstdlib>

namespace
{
const char* ErrorText(outcome result)
{
    if (result.ok())
        return "";
    const char* text = find_err_mess(result.error_number());
    return text == NULL ? "(unknown)" : text;
}

logical CheckOutcome(const char* name, outcome result)
{
    if (result.ok())
        return TRUE;
    std::printf("%s failed: %s\n", name, ErrorText(result));
    return FALSE;
}

logical InitializeBaseAndOptionalUnlock()
{
    base_configuration base_config;
    logical base_ok = initialize_base(&base_config);
    if (base_ok == FALSE)
    {
        std::printf("initialize_base failed\n");
        return FALSE;
    }

    const char* unlock_str = std::getenv("SPATIAL_LICENSE");
    if (unlock_str == NULL || unlock_str[0] == '\0')
        return TRUE;

    spa_unlock_result unlock_result = spa_unlock_products(unlock_str);
    if (unlock_result.get_state() == SPA_UNLOCK_FAIL)
    {
        std::printf("spa_unlock_products failed: %s\n", unlock_result.get_message_text());
        return FALSE;
    }
    return TRUE;
}

logical StartAcis()
{
    if (!InitializeBaseAndOptionalUnlock())
        return FALSE;
    if (!CheckOutcome("api_start_modeller", api_start_modeller(0)))
        return FALSE;
    if (!CheckOutcome("api_initialize_kernel", api_initialize_kernel()))
        return FALSE;
    if (!CheckOutcome("api_initialize_constructors", api_initialize_constructors()))
        return FALSE;
    if (!CheckOutcome("api_initialize_offsetting", api_initialize_offsetting()))
        return FALSE;
    if (!CheckOutcome("api_initialize_booleans", api_initialize_booleans()))
        return FALSE;
    if (!CheckOutcome("api_initialize_generic_attributes", api_initialize_generic_attributes()))
        return FALSE;
    return TRUE;
}

void StopAcis()
{
    (void)api_terminate_booleans();
    (void)api_terminate_offsetting();
    (void)api_terminate_constructors();
    (void)api_terminate_generic_attributes();
    (void)api_terminate_kernel();
    (void)api_stop_modeller();
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::printf("usage: %s <config.yaml>\n", argv[0]);
        return 1;
    }

    if (StartAcis() == FALSE)
        return 1;

    midsurface_new::MidSurfaceConfig config;
    if (midsurface_new::LoadMidSurfaceConfigFromYaml(argv[1], config) == FALSE)
    {
        std::printf("LoadMidSurfaceConfigFromYaml failed: %s\n", argv[1]);
        StopAcis();
        return 1;
    }

    midsurface_new::MidSurfaceResult result;
    const logical ok = midsurface_new::RunMidSurface(config, result);

    StopAcis();
    return ok != FALSE ? 0 : 2;
}
