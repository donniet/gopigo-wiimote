#include <iostream>

#include <bluetooth/bluetooth.h>
#include <cwiid.h>

#include <signal.h>

#include <stdio.h>
#include <stdlib.h>
#include <cmath>
#include <boost/numeric/ublas/vector.hpp>
#include <boost/numeric/ublas/matrix.hpp>
#include <boost/numeric/ublas/io.hpp>

#include "include/simple_socket.hpp"
#include "include/channel.hpp"

using std::atan2;
using std::sqrt;

using namespace boost::numeric::ublas;


using std::cout;
using std::cerr;
using std::endl;

template<typename ... Ts>
struct pack_size {};

template<> struct pack_size<> {
    enum { size = 0 };
};

template<typename T, typename ... Ts>
struct pack_size<T, Ts...> {
    enum { size = pack_size<Ts...>::size + 1 };
};

template<typename T, typename ... Ts>
vector<T> create_vector(T const & t, Ts const & ... ts) {

}


void ir_callback(cwiid_wiimote_t * wiimote,
                 int num,
                 union cwiid_mesg mesgs[],
                 struct timespec * time)
{
}

void err(cwiid_wiimote_t * wiimote, const char * s, va_list ap)
{
    if(wiimote) printf("%d:", cwiid_get_id(wiimote));
    else printf("-1:");

    vprintf(s, ap);
    printf("\n");
}

channel<union cwiid_mesg> mesgs;

void cwiid_callback(cwiid_wiimote_t * wiimote, int msg_count,
                    union cwiid_mesg mesg[], struct timespec * timestamp)
{
    for(int i = 0; i < msg_count; i++) {
        mesgs.send(mesg[i]);
    }
}

std::mutex inter_mutex;
bool interrupted;

void handle_signal(int) {
    if (interrupted) exit(-1);

    std::unique_lock<std::mutex> lock(inter_mutex);
    interrupted = true;
}


int main(int argc, char * argv[]) {
    interrupted = false;
    double height = 1;
    if (argc > 1) height = atof(argv[1]);


    float invcam[9] = {
        tan(15.*M_PI/180.) / 384., 0, -512./384.*tan(15.*M_PI/180.),
        0, tan(15.*M_PI/180.)/384., -tan(15.*M_PI/180.),
        0, 0, 1
    };

    matrix<double> camA(3,3);
    for(int i = 0; i < 9; i++) {
        camA(i / 3, i % 3) = invcam[i];
    }

    cout << "camA: " << camA << endl;

    matrix<double> r = identity_matrix<double>(3,3);
    matrix<double> r_ = identity_matrix<double>(3,3);


    signal(SIGINT, handle_signal);
    cout << "Looking for wiimote, press 1+2..." << endl;

    cwiid_wiimote_t * wiimote;
    bdaddr_t bdaddr;

    if (argc < 2) {
        bdaddr = *BDADDR_ANY;
    }
    else {
        str2ba(argv[1], &bdaddr);
    }

    cwiid_set_err(err);

    if ((wiimote = cwiid_open(&bdaddr, 0)) == 0) {
        cerr << "error opening" << endl;
        return -1;
    }
    if (cwiid_set_led(wiimote, CWIID_LED1_ON | CWIID_LED2_ON | CWIID_LED3_ON | CWIID_LED4_ON)) {
        cerr << "error turning leds on" << endl;
        return -1;
    }

    if(cwiid_set_mesg_callback(wiimote, cwiid_callback)) {
        cerr << "error setting callback" << endl;
        return -1;
    }
    if(cwiid_set_rpt_mode(wiimote, CWIID_RPT_IR | CWIID_RPT_ACC)) {
        cerr << "error setting rpt mode" << endl;
        return -1;
    }
    if(cwiid_enable(wiimote, CWIID_FLAG_MESG_IFC)) {
        cerr << "error enabling messages." << endl;
        return -1;
    }

    union cwiid_mesg mesg;

    cout << "remote found, starting message loop" << endl;




    while(mesgs.recv(mesg)) {
        if (mesg.type == CWIID_MESG_IR) {
            bool is_valid = false;
            for(int j = 0; j < CWIID_IR_SRC_COUNT; j++) {
                if (mesg.ir_mesg.src[j].valid) {
                    is_valid = true;
                    //cout << "IR (" << mesg.ir_mesg.src[j].pos[CWIID_X] << "," << mesg.ir_mesg.src[j].pos[CWIID_Y] << ") ";
                    vector<double> p(3);
                    p(0) = mesg.ir_mesg.src[j].pos[CWIID_X];
                    p(1) = mesg.ir_mesg.src[j].pos[CWIID_Y];
                    p(2) = 1.;

                    p = prod(camA, p);
                    p(2) = p(1);
                    p(1) = -1;
                    p(0) = -p(0);

                    //cout << "P: " << p << endl;

                    p /= norm_2(p);
                    //cout << "uP: " << p << endl;

                    p = prod(r_,p);

                    //cout << "r_: " << r_ << endl;

                    //cout << "rP: " << p << endl;

                    p = p / p(2) * height;

                    cout << "x: " << p(0) << " y: " << p(1) << endl;

                }
            }
            if (is_valid) cout << endl;
        } else if (mesg.type == CWIID_MESG_ACC) {
            //cout << "ACC (" << (short)mesg.acc_mesg.acc[0] << "," << (short)mesg.acc_mesg.acc[1] << "," << (short)mesg.acc_mesg.acc[2] << ")" << endl;


            r(2,0) = ((float)mesg.acc_mesg.acc[0] - 118.0f) / 24.0f;
            r(2,1) = ((float)mesg.acc_mesg.acc[1] - 118.0f) / 24.0f;
            r(2,2) = ((float)mesg.acc_mesg.acc[2] - 118.0f) / 24.0f;

            float ds = sqrt(r(2,0) * r(2,0) + r(2,1) * r(2,1) + r(2,2) * r(2,2));
            if (ds == 0.0f) {
                r(2,0) = 0; r(2,1) = 0; r(2,2) = 1;
            } else {
                r(2,0) /= ds; r(2,1) /= ds; r(2,2) /= ds;
            }

            r(0,0) = r(2,2);
            r(0,1) = 0;
            r(0,2) = -r(2,1);

            float ls = sqrt(r(0,0) * r(0,0) + r(0,1) * r(0,1) + r(0,2) * r(0,2));

            if (ls == 0.0f) {
                r(0,0) = 1; r(0,1) = 0; r(0,2) = 0;
            } else {
                r(0,0) /= ls; r(0,1) /= ls; r(0,2) /= ls;
            }

            r(1,0) = r(2,1) * r(0,2) - r(2,2) * r(0,1);
            r(1,1) = r(2,2) * r(0,0) - r(2,0) * r(0,2);
            r(1,2) = r(2,0) * r(0,1) - r(2,1) * r(0,0);

            r_ = trans(r);
            swap(r_, r);

            //cout << "r: " << r << endl;

        } else if (mesg.type == CWIID_MESG_MOTIONPLUS) {
            cout << "ROLL: " << mesg.motionplus_mesg.angle_rate[0] << " PITCH: " << mesg.motionplus_mesg.angle_rate[1] << endl;
        }

        std::unique_lock<std::mutex> lock(inter_mutex);
        if (interrupted) {
            break;
        }
    }

    if (cwiid_close(wiimote)) {
        cerr << "error on disconnect" << endl;
        return -1;
    }

    cout << "done." << endl;

    return 0;


}
