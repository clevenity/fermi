#include "IOError.h"

#include <errno.h>


const char* IOError_toString(IOErrorKind error) {
    switch (error) {
        case IO_OK:
            return "operation completed successfully";
        case IO_NOT_FOUND:
            return "no such file or directory";
        case IO_PERMISSION_DENIED:
            return "permission denied; insufficient access rights";
        case IO_ALREADY_EXISTS:
            return "file already exists at the specified path";
        case IO_INVALID_PATH:
            return "invalid path specification";
        case IO_READ_FAILED:
            return "read operation failed";
        case IO_WRITE_FAILED:
            return "write operation failed";
        case IO_UNEXPECTED_EOF:
            return "unexpected end-of-file encountered";
        case IO_INVALID_DATA:
            return "invalid data format";
        case IO_OUT_OF_MEMORY:
            return "memory allocation failed; insufficient memory available";
        case IO_OUT_OF_SPACE:
            return "write failed; no space remaining on device";
        case IO_INTERRUPTED:
            return "operation interrupted by signal";
        case IO_TIMEOUT:
            return "operation exceeded time limit";
        default:
            return "unspecified I/O error";
    }
}


IOErrorKind IOError_fromErrno(int err) {
    switch (err) {
        case ENOENT:
            return IO_NOT_FOUND;

        case EACCES:
            return IO_PERMISSION_DENIED;

        case EEXIST:
            return IO_ALREADY_EXISTS;

        case ENOSPC:
            return IO_OUT_OF_SPACE;

        case ENOMEM:
            return IO_OUT_OF_MEMORY;

        case EINTR:
            return IO_INTERRUPTED;

        default:
            return IO_UNKNOWN_ERROR;
    }
}


bool IOError_isFatal(IOErrorKind error) {
    switch (error) {
        case IO_OUT_OF_MEMORY:
        case IO_OUT_OF_SPACE:
            return true;

        default:
            return false;
    }
}