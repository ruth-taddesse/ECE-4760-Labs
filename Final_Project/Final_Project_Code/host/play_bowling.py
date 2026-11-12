"""USB terminal for BOWLING_DEMO.uf2; the Pico runs capture, classification and physics."""
import argparse
import serial
from serial.tools import list_ports
from collect import Link


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port')
    args=parser.parse_args()
    if not args.port:
        for p in list_ports.comports():print(p.device,p.description)
        args.port=input('Pico port: ').strip()
    print('Close the Serial Monitor before continuing. Flash build/BOWLING_DEMO.uf2.')
    with serial.Serial(args.port,115200,timeout=.1,write_timeout=2) as device:
        device.dtr=True
        device.write(b'STOP\n')
        link=Link(device);link.send('INFO')
        info=link.response('BOWL,',5)
        if len(info)!=4 or info[1]!='1' or info[2]!='1':raise ValueError(f'Wrong firmware or sensor failure: {info}')
        while True:
            if input('Place sensor +Z up on table. Enter to calibrate; q to quit: ').strip().lower()=='q':return
            link.send('CAL')
            try:print(','.join(link.response('CAL,',6)));break
            except ValueError as e:print(e)
        print('Enter starts a throw. Watch VGA: 3s countdown, HOLD STILL, THROW NOW, HOLD STILL.')
        print('Use SET start_x -0.5, SET hook_gain 1.5, SET speed_gain 1.1, REPLAY or RESET.')
        print('Tuning replays the last throw. q quits. This demo does not save collection CSVs.')
        while True:
            command=input('Enter to throw, or command: ').strip()
            if command.lower()=='q':return
            # Accept case-insensitive command words, preserve lowercase parameter names.
            parts=command.split(maxsplit=1)
            command=(parts[0].upper()+(' '+parts[1] if len(parts)>1 else '')) if parts else 'THROW'
            link.send(command)
            try:
                if command=='THROW':
                    print(','.join(link.response('ACK,',3)))
                    print(','.join(link.response('RESULT,',12)))
                    print('Watch the VGA ball trail; then press Enter for another throw.')
                else:
                    print(','.join(link.response('CAL,' if command=='CAL' else 'BOWL,' if command=='INFO' else 'OK,',6)))
            except (ValueError,TimeoutError) as error:print(error)


if __name__=='__main__':
    try:main()
    except (KeyboardInterrupt,EOFError):print('\nBowling demo closed.')
    except (serial.SerialException,ValueError,TimeoutError) as e:print(f'Connection/demo error: {e}')
