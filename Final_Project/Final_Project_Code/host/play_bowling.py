"""USB terminal for wired BOWLING_DEMO or wireless SPORTS_GROUND firmware."""
import argparse
import time
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
    print('Close Serial Monitor. Use the game board USB port (ground station for the wireless pair).')
    with serial.Serial(args.port,115200,timeout=.1,write_timeout=2) as device:
        device.dtr=True
        device.write(b'STOP\n')
        link=Link(device);link.send('INFO')
        deadline=time.monotonic()+5
        while True:
            reply=link.line(deadline)
            if reply.startswith('INFO,'):
                raise ValueError('Collection firmware is loaded. Flash build/BOWLING_DEMO.uf2 for the game menu.')
            if reply.startswith('ERR,'):
                raise ValueError(reply)
            if reply.startswith('BOWL,'):
                info=reply.split(',')
                break
        if len(info)!=4 or info[1]!='1':raise ValueError(f'Wrong firmware: {info}')
        link.send('MENU')
        print(','.join(link.response('OK,',3)))
        print('VGA menu: 1 Bowling / 2 Basketball / 3 Cup pong / 4 Settings / 5 Quit.')
        print('Type W then Enter to move up, or S then Enter to move down.')
        print('Press Enter to select the highlighted option. MENU returns to game selection.')
        print('In Bowling: CAL calibrates (+Z up, still), then Enter starts a throw.')
        print('SETTINGS opens tuning; BACK returns. QUIT (or q) ends the session.')
        print('This prototype does not save collection CSVs.')
        while True:
            text=input('Pico Sports > ').strip()
            parts=text.split(maxsplit=1)
            command=(parts[0].upper()+(' '+parts[1] if len(parts)>1 else '')) if parts else ''
            if command=='Q':command='QUIT'
            link.send(command)
            try:
                # Menu Enter returns OK; game Enter starts a throw and returns ACK.
                deadline=time.monotonic()+6
                while True:
                    reply=link.line(deadline)
                    if reply.startswith('ERR,'):raise ValueError(reply)
                    if reply.startswith(('OK,','ACK,','CAL,','BOWL,')):break
                print(reply)
                if reply.startswith('ACK,CAL'):
                    print(','.join(link.response('CAL,',8)))
                elif reply.startswith('ACK,'):
                    print(','.join(link.response('RESULT,',12)))
                    print('Watch the VGA ball trail; Enter bowls again, MENU returns.')
                if command in ('QUIT','5'):return
            except (ValueError,TimeoutError) as error:print(error)


if __name__=='__main__':
    try:main()
    except (KeyboardInterrupt,EOFError):print('\nTerminal closed. Use QUIT before closing to stop the session on the Pico.')
    except (serial.SerialException,ValueError,TimeoutError) as e:print(f'Connection/demo error: {e}')
