import os
import glob
import re

includes_dir = '/home/kunsh/Cpp/Snap/snap-windows/includes/'
files = glob.glob(includes_dir + '*.hpp') + ['/home/kunsh/Cpp/Snap/snap-windows/snap.hpp']

for filepath in files:
    with open(filepath, 'r') as f:
        content = f.read()
    
    # Headers
    content = content.replace('#include <sys/socket.h>', '#include <winsock2.h>\n#include <ws2tcpip.h>\n#pragma comment(lib, "ws2_32.lib")')
    content = content.replace('#include <netinet/tcp.h>', '')
    content = content.replace('#include <arpa/inet.h>', '')
    content = content.replace('#include <unistd.h>', '#include <io.h>')
    content = content.replace('#include <fcntl.h>', '')
    content = content.replace('#include <poll.h>', '')
    
    # Types & Constants
    content = content.replace('ssize_t ', 'int ')
    content = content.replace(' MSG_DONTWAIT', ' 0')
    content = content.replace('MSG_NOSIGNAL | 0', '0')
    content = content.replace('MSG_NOSIGNAL', '0')
    content = content.replace('SO_REUSEPORT', 'SO_REUSEADDR')
    
    # Functions
    content = content.replace('close(_fd)', 'closesocket(_fd)')
    content = content.replace('close(fd)', 'closesocket(fd)')
    content = content.replace('::close(', 'closesocket(')
    content = content.replace(' poll(', ' WSAPoll(')
    
    # Non-blocking socket
    content = re.sub(r'int flags = fcntl\([^,]+, F_GETFL, 0\);\s*fcntl\(([^,]+), F_SETFL, flags \| O_NONBLOCK\);', r'u_long mode = 1; ioctlsocket(\1, FIONBIO, &mode);', content)
    
    # Remove SO_BUSY_POLL (Linux specific)
    content = re.sub(r'int poll = 50;\s*setsockopt\(_fd, SOL_SOCKET, SO_BUSY_POLL, &poll, sizeof\(poll\)\);', '', content)
    
    with open(filepath, 'w') as f:
        f.write(content)

print("Winsock replacements done.")
