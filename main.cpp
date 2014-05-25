#include <unistd.h>
#include <strings.h>
#include <stdint.h>
#include <assert.h>

#include "jack_client.h"
#include "sensor.h"

void copy_data_to_jack(OPIPKT_t &opipkt)
{
    unsigned int pb = 9;
    for (unsigned int adcc = 0; adcc < (opipkt.length - 17) / 2; adcc++)
    {
        jack_append_new_data( ((int16_t) opipkt.payload[pb++]) << 8 + ((int16_t) opipkt.payload[pb++] & 0xFC) );
    }

    //let's not do crc, we don't really need correct data anyway ... it's art ;->
}

int main(int argc, char* argv[])
{
    HANDLE comprt;
    OPIPKT_t onepkt;

    int rc = init_openucd_and_module(comprt);
    if (rc != 0)
    {
        opi_closeucd_com(&comprt);
        return rc;
    }

    //jack_init();
    //jack_run();
    while (1)
    {
        //rc = opiucd_status(&comprt, &onepkt);
        bzero(&onepkt, sizeof(OPIPKT_t));
        rc = opiucd_getwltsdata(&comprt, &onepkt);
        if (rc == 1)
        {
            opipkt_dump(&onepkt);
            interpret_data_packet(onepkt);
            //copy_data_to_jack(onepkt);
        }
        else if (rc == 0)
        {
            //printf("got OK/NoData\n");
        }
        else
            printf("status = %d\n", rc);
    }
    jack_byebye();
    opi_closeucd_com(&comprt);
    return rc;
}
