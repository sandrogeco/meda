
import socket
import numpy as np
import struct
import numpy as np
import obspy
from obspy import UTCDateTime
from obspy.core import Trace, Stream, UTCDateTime
import sys

import queue
from threading import Thread
import time
import os.path
import subprocess
from pathlib import Path

mSeedPath='/home/leonardo/acq_scripts/temp/'#/mnt/mseed/'
mSeedTmpPath = 'temp/'


queueLst = {}
threadLst = []

block_length = 5

network = 'ST'
station = 'THK'
location = '00'
chtype = 'D'


def streamBuilder(network, station, location, channel, data, npts, tstart, smp, rsp):
    try:
        stats = {'network': network, 'station': station, 'location': location,
                 'channel': channel, 'npts': npts, 'sampling_rate': smp,
                 'mseed': {'dataquality': 'D'}, 'starttime': tstart}
        stream = Stream([Trace(data=np.asarray(data), header=stats)])
        print(stream)
        # stream.resample(rsp)
        if rsp >0:
            stream.interpolate(rsp)
        # < SDSdir > / Year / NET / STA / CHAN.TYPE / NET.STA.LOC.CHAN.TYPE.YEAR.DAY
        tt = UTCDateTime(tstart)
        fol = mSeedPath + "/" + tt.strftime("%Y") + "/" + network + "/" + station + "/" + channel + "." + chtype + "/"
        Path(fol).mkdir(parents=True, exist_ok=True)
        File = fol + network + "." + station + "." + location + "." + channel + "." + chtype + "." + tt.strftime("%Y.%j")
        temp_file = mSeedTmpPath + channel + UTCDateTime.now().strftime("%S") + ".temp.tmp"
        if os.path.isfile(File):
            stream.write(temp_file, format='MSEED', reclen=512)
            subprocess.call("cat " + temp_file + " >> " + File + " && rm " + temp_file, shell=True)
        else:
            stream.write(File, format='MSEED', reclen=512)
    except:
        stream=Stream()
        print('ex')
    return stream

def streamBuilder_tmp(network, station, location, channel, data, npts, tstart, smp,rsp):
    try:
        stats = {'network': network, 'station': station, 'location': location,
                 'channel': channel, 'npts': npts, 'sampling_rate': smp,
                 'mseed': {'dataquality': 'D'}, 'starttime': tstart}
        stream = Stream([Trace(data=np.asarray(data,dtype=np.int32), header=stats)])
        temp_file = mSeedPath + network+station+location+channel+UTCDateTime.now().strftime("%d")+".mseed"
        if os.path.isfile(temp_file):
            print(stream[0].data)
            stream.write(temp_file+".m", format='MSEED',reclen=512,encoding='INT32')
            subprocess.call("cat "+temp_file+".m >> "+temp_file+" && rm "+temp_file+".m",shell=True)
        else:
            stream.write(temp_file,format='MSEED',reclen=512,encoding='INT32')
        print(stream)
    except Exception as e:
        stream=Stream()
        print('ex ',e)
    for f in os.listdir(mSeedPath):
        if os.path.isfile(mSeedPath+f):
            if(UTCDateTime.now()-os.path.getmtime(mSeedPath+f))>24*3600:
                os.remove(mSeedPath+f)

    return stream

def save_data(queue: queue.Queue):
    def decrypt(yy):
        xx=yy[0]
#        s=''.join(chr(i) for i in x)
        ss=xx.split(',')
        x=((5*int(ss[0]))/(2**24-1)-2.5)*20
        y=((5*int(ss[1]))/(2**24-1)-2.5)*20
        volt=int(ss[3])*21*2.5/(2**12-1)+0.3
        temp=int(ss[2])/32
        x=int(ss[2])
        y=int(ss[1])
        temp=int(ss[3])
        volt=int(ss[0])
 #       print(x/137942,'',y/138076,'',volt,'',temp)
        t = yy[1]
        return x,y,volt,temp,t
    c=[]
    xs=[]
    ys=[]
    volts=[]
    temps=[]
    tts=[]
    tOld=UTCDateTime.now()
    t=0
    while True:
        s=queue.get()
        queue.task_done()
        try:
            [x,y,volt,temp,ts]=decrypt(s)
            l = len(xs)
            gap=ts-t>8
            print('diff '+str(ts-t)+' '+str(gap))
            t+=1
            if gap:
                t=tts[0]
            if ts-t<0:
                t-=1
            print(t)    
            if (l >= block_length)or gap:
                # if (tts[0]-tOld)<=1:
                #     tts[0]=tOld+1
                #     print('time ok')
                # else:
                #     print('time NOK')
                streamBuilder_tmp(network, station, location, 'LAY', xs, len(xs), t,
                            1 , 1)
                streamBuilder_tmp(network, station, location, 'LAX', ys, len(ys), t,
                                1, 1)
                streamBuilder_tmp(network, station, location, 'LQV', volts, len(volts), t,
                                1, 1)
                streamBuilder_tmp(network, station, location, 'LKD', temps, len(temps), t,
                                1, 1)
                tOld=tOld+5
                tts=[]
                xs=[]
                ys=[]
                temps=[]
                volts=[]
        except:
            pass
        xs.append(x)
        ys.append(y)
        volts.append(volt)
        temps.append(temp)
        tts.append(UTCDateTime(ts))

def subfinder(mylist, pattern):
    matches = []
    ii=-1
    for i in range(len(mylist)):
        if mylist[i] == pattern[0] and mylist[i:i+len(pattern)] == pattern:       
            ii=i
    return ii

HOST = "192.168.1.64"  # The server's hostname or IP address
PORT = 950  # The port used by the server

queueLst = queue.Queue()
queueBuf = queue.Queue()
trh = Thread(target=save_data, args=[queueLst])
trh.start()
data=[]
t0=UTCDateTime.now()

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.settimeout(10)
    while True:
        try:
            s.connect((HOST, PORT))
            while True:
                ss=s.recv(1)
                data+=ss 
                if ss==b'\n':
                   datastr=''.join([chr(d) for d in data[0:-2]])
                   print(datastr)
                   tx=UTCDateTime.now()
                   tx.microsecond=0
                   queueLst.put([datastr[0:-2],tx])
                   data=[]
        except:
            print('e')
            time.sleep(30)
            pass

