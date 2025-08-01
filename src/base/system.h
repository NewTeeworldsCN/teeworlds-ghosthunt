/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

/*
	Title: OS Abstraction
*/

#ifndef BASE_SYSTEM_H
#define BASE_SYSTEM_H

#include "detect.h"
#include <cstdint>
#include <ctime>

using std::size_t;

#ifdef __GNUC__
#define GNUC_ATTRIBUTE(x) __attribute__(x)
#else
#define GNUC_ATTRIBUTE(x)
#endif

/* Group: Debug */
/*

	Function: dbg_assert
		Breaks into the debugger based on a test.

	Parameters:
		test - Result of the test.
		msg - Message that should be printed if the test fails.

	See Also:
		<dbg_break>
*/
void dbg_assert(int test, const char *msg);
#define dbg_assert(test, msg) dbg_assert_imp(__FILE__, __LINE__, test, msg)
void dbg_assert_imp(const char *filename, int line, int test, const char *msg);

#ifdef __clang_analyzer__
#include <assert.h>
#undef dbg_assert
#define dbg_assert(test, msg) assert(test)
#endif

/*
	Function: dbg_break
		Breaks into the debugger.

	See Also:
		<dbg_assert>
*/
void dbg_break();

/*
	Function: dbg_msg

	Prints a debug message.

	Parameters:
		sys - A string that describes what system the message belongs to
		fmt - A printf styled format string.

	See Also:
		<dbg_assert>
*/
void dbg_msg(const char *sys, const char *fmt, ...)
	GNUC_ATTRIBUTE((format(printf, 2, 3)));

/* Group: Memory */

/*
	Function: mem_alloc
		Allocates memory.

	Parameters:
		size - Size of the needed block.

	Returns:
		Returns a pointer to the newly allocated block. Returns a
		null pointer if the memory couldn't be allocated.

	Remarks:
		- The behavior when passing 0 as size is unspecified.

	See Also:
		<mem_free>
*/
void *mem_alloc(unsigned size);

/*
	Function: mem_free
		Frees a block allocated through <mem_alloc>.

	See Also:
		<mem_alloc>
*/
void mem_free(void *block);

/*
	Function: mem_copy
		Copies a a memory block.

	Parameters:
		dest - Destination.
		source - Source to copy.
		size - Size of the block to copy.

	Remarks:
		- This functions DOES NOT handles cases where source and
		destination is overlapping.

	See Also:
		<mem_move>
*/
void mem_copy(void *dest, const void *source, unsigned size);

/*
	Function: mem_move
		Copies a a memory block

	Parameters:
		dest - Destination
		source - Source to copy
		size - Size of the block to copy

	Remarks:
		- This functions handles cases where source and destination
		is overlapping

	See Also:
		<mem_copy>
*/
void mem_move(void *dest, const void *source, unsigned size);

/*
	Function: mem_zero
		Sets a complete memory block to 0

	Parameters:
		block - Pointer to the block to zero out
		size - Size of the block
*/
void mem_zero(void *block, unsigned size);

/*
	Function: mem_comp
		Compares two blocks of memory

	Parameters:
		a - First block of data
		b - Second block of data
		size - Size of the data to compare

	Returns:
		<0 - Block a is lesser then block b
		0 - Block a is equal to block b
		>0 - Block a is greater then block b
*/
int mem_comp(const void *a, const void *b, int size);

/*
	Function: mem_has_null
		Checks whether a block of memory contains null bytes.

	Parameters:
		block - Pointer to the block to check for nulls.
		size - Size of the block.

	Returns:
		1 - The block has a null byte.
		0 - The block does not have a null byte.
*/
int mem_has_null(const void *block, unsigned size);

/* Group: File IO */
enum
{
	IOFLAG_READ = 1,
	IOFLAG_WRITE = 2,
	IOFLAG_APPEND = 4,
	IOFLAG_SKIP_BOM = 8,

	IOSEEK_START = 0,
	IOSEEK_CUR = 1,
	IOSEEK_END = 2,

	IO_MAX_PATH_LENGTH = 512,
};

typedef struct IOINTERNAL *IOHANDLE;

/*
	Function: io_open
		Opens a file.

	Parameters:
		filename - File to open.
		flags - A set of flags. IOFLAG_READ, IOFLAG_WRITE, IOFLAG_APPEND, IOFLAG_SKIP_BOM.

	Returns:
		Returns a handle to the file on success and 0 on failure.

*/
IOHANDLE io_open(const char *filename, int flags);

/*
	Function: io_read
		Reads data into a buffer from a file.

	Parameters:
		io - Handle to the file to read data from.
		buffer - Pointer to the buffer that will receive the data.
		size - Number of bytes to read from the file.

	Returns:
		Number of bytes read.

*/
unsigned io_read(IOHANDLE io, void *buffer, unsigned size);

/*
	Function: io_read_all
		Reads the rest of the file into a buffer.

	Parameters:
		io - Handle to the file to read data from.
		result - Receives the file's remaining contents.
		result_len - Receives the file's remaining length.

	Remarks:
		- Does NOT guarantee that there are no internal null bytes.
		- The result must be freed after it has been used.
*/
void io_read_all(IOHANDLE io, void **result, unsigned *result_len);

/*
	Function: io_read_all_str
		Reads the rest of the file into a zero-terminated buffer with
		no internal null bytes.

	Parameters:
		io - Handle to the file to read data from.

	Returns:
		The file's remaining contents or null on failure.

	Remarks:
		- Guarantees that there are no internal null bytes.
		- Guarantees that result will contain zero-termination.
		- The result must be freed after it has been used.
*/
char *io_read_all_str(IOHANDLE io);

/*
	Function: io_unread_byte
		"Unreads" a single byte, making it available for future read
		operations.

	Parameters:
		io - Handle to the file to unread the byte from.
		byte - Byte to unread.

	Returns:
		Returns 0 on success and 1 on failure.

*/
unsigned io_unread_byte(IOHANDLE io, unsigned char byte);

/*
	Function: io_skip
		Skips data in a file.

	Parameters:
		io - Handle to the file.
		size - Number of bytes to skip.

	Returns:
		Number of bytes skipped.
*/
unsigned io_skip(IOHANDLE io, int size);

/*
	Function: io_write
		Writes data from a buffer to file.

	Parameters:
		io - Handle to the file.
		buffer - Pointer to the data that should be written.
		size - Number of bytes to write.

	Returns:
		Number of bytes written.
*/
unsigned io_write(IOHANDLE io, const void *buffer, unsigned size);

/*
	Function: io_write_newline
		Writes newline to file.

	Parameters:
		io - Handle to the file.

	Returns:
		Number of bytes written.
*/
unsigned io_write_newline(IOHANDLE io);

/*
	Function: io_seek
		Seeks to a specified offset in the file.

	Parameters:
		io - Handle to the file.
		offset - Offset from pos to stop.
		origin - Position to start searching from.

	Returns:
		Returns 0 on success.
*/
int io_seek(IOHANDLE io, int offset, int origin);

/*
	Function: io_tell
		Gets the current position in the file.

	Parameters:
		io - Handle to the file.

	Returns:
		Returns the current position. -1L if an error occured.
*/
long int io_tell(IOHANDLE io);

/*
	Function: io_length
		Gets the total length of the file. Resetting cursor to the beginning

	Parameters:
		io - Handle to the file.

	Returns:
		Returns the total size. -1L if an error occured.
*/
long int io_length(IOHANDLE io);

/*
	Function: io_close
		Closes a file.

	Parameters:
		io - Handle to the file.

	Returns:
		Returns 0 on success.
*/
int io_close(IOHANDLE io);

/*
	Function: io_flush
		Empties all buffers and writes all pending data.

	Parameters:
		io - Handle to the file.

	Returns:
		Returns 0 on success.
*/
int io_flush(IOHANDLE io);

/*
	Function: io_error
		Checks whether an error occurred during I/O with the file.

	Parameters:
		io - Handle to the file.

	Returns:
		Returns nonzero on error, 0 otherwise.
*/
int io_error(IOHANDLE io);

/*
	Function: io_stdin
		Returns an <IOHANDLE> to the standard input.
*/
IOHANDLE io_stdin();

/*
	Function: io_stdout
		Returns an <IOHANDLE> to the standard output.
*/
IOHANDLE io_stdout();

/*
	Function: io_stderr
		Returns an <IOHANDLE> to the standard error.
*/
IOHANDLE io_stderr();

/* Group: Asychronous I/O */

typedef struct ASYNCIO ASYNCIO;
/*
	Function: aio_new
		Wraps a <IOHANDLE> for asynchronous writing.

	Parameters:
		io - Handle to the file.

	Returns:
		Returns the handle for asynchronous writing.

*/
ASYNCIO *aio_new(IOHANDLE io);

/*
	Function: aio_lock
		Locks the ASYNCIO structure so it can't be written into by
		other threads.

	Parameters:
		aio - Handle to the file.
*/
void aio_lock(ASYNCIO *aio);

/*
	Function: aio_unlock
		Unlocks the ASYNCIO structure after finishing the contiguous
		write.

	Parameters:
		aio - Handle to the file.
*/
void aio_unlock(ASYNCIO *aio);

/*
	Function: aio_write
		Queues a chunk of data for writing.

	Parameters:
		aio - Handle to the file.
		buffer - Pointer to the data that should be written.
		size - Number of bytes to write.

*/
void aio_write(ASYNCIO *aio, const void *buffer, unsigned size);

/*
	Function: aio_write_newline
		Queues a newline for writing.

	Parameters:
		aio - Handle to the file.

*/
void aio_write_newline(ASYNCIO *aio);

/*
	Function: aio_write_unlocked
		Queues a chunk of data for writing. The ASYNCIO struct must be
		locked using `aio_lock` first.

	Parameters:
		aio - Handle to the file.
		buffer - Pointer to the data that should be written.
		size - Number of bytes to write.

*/
void aio_write_unlocked(ASYNCIO *aio, const void *buffer, unsigned size);

/*
	Function: aio_write_newline_unlocked
		Queues a newline for writing. The ASYNCIO struct must be locked
		using `aio_lock` first.

	Parameters:
		aio - Handle to the file.

*/
void aio_write_newline_unlocked(ASYNCIO *aio);

/*
	Function: aio_error
		Checks whether errors have occurred during the asynchronous
		writing.

		Call this function regularly to see if there are errors. Call
		this function after <aio_wait> to see if the process of writing
		to the file succeeded.

	Parameters:
		aio - Handle to the file.

	Returns:
		Returns 0 if no error occurred, and nonzero on error.

*/
int aio_error(ASYNCIO *aio);

/*
	Function: aio_close
		Queues file closing.

	Parameters:
		aio - Handle to the file.

*/
void aio_close(ASYNCIO *aio);

/*
	Function: aio_wait
		Wait for the asynchronous operations to complete.

	Parameters:
		aio - Handle to the file.

*/
void aio_wait(ASYNCIO *aio);

/*
	Function: aio_free
		Frees the resources associated to the asynchronous file handle.

	Parameters:
		aio - Handle to the file.

*/
void aio_free(ASYNCIO *aio);

/* Group: Threads */

/*
	Function: thread_sleep
		Suspends the current thread for a given period.

	Parameters:
		milliseconds - Number of milliseconds to sleep.
*/
void thread_sleep(int milliseconds);

/*
	Function: thread_init
		Creates a new thread.

	Parameters:
		threadfunc - Entry point for the new thread.
		user - Pointer to pass to the thread.

*/
void *thread_init(void (*threadfunc)(void *), void *user);

/*
	Function: thread_wait
		Waits for a thread to be done or destroyed.

	Parameters:
		thread - Thread to wait for.
*/
void thread_wait(void *thread);

/*
	Function: thread_destroy
		Frees resources associated with a thread handle.

	Parameters:
		thread - Thread handle to destroy.

	Remarks:
		- The thread must have already terminated normally.
		- Detached threads must not be destroyed with this function.
*/
void thread_destroy(void *thread);

/*
	Function: thread_yield
		Yield the current threads execution slice.
*/
void thread_yield();

/*
	Function: thread_detach
		Puts the thread in the detached state, guaranteeing that
		resources of the thread will be freed immediately when the
		thread terminates.

	Parameters:
		thread - Thread to detach

	Remarks:
		- This invalidates the thread handle, hence it must not be
		used after detaching the thread.
*/
void thread_detach(void *thread);

/*
	Function: cpu_relax
		Lets the cpu relax a bit.
*/
void cpu_relax();

/* Group: Locks */
typedef void *LOCK;

LOCK lock_create();
void lock_destroy(LOCK lock);

int lock_trylock(LOCK lock);
void lock_wait(LOCK lock);
void lock_unlock(LOCK lock);

/* Group: Semaphores */

#if defined(CONF_FAMILY_UNIX) && !defined(CONF_PLATFORM_MACOS)
#include <semaphore.h>
typedef sem_t SEMAPHORE;
#elif defined(CONF_FAMILY_WINDOWS)
typedef void *SEMAPHORE;
#else
typedef struct SEMINTERNAL *SEMAPHORE;
#endif

void sphore_init(SEMAPHORE *sem);
void sphore_wait(SEMAPHORE *sem);
void sphore_signal(SEMAPHORE *sem);
void sphore_destroy(SEMAPHORE *sem);

/* Group: Timer */
/*
	Function: time_get
		Fetches a sample from a high resolution timer.

	Returns:
		Current value of the timer.

	Remarks:
		To know how fast the timer is ticking, see <time_freq>.
*/
int64_t time_get();

/*
	Function: time_freq
		Returns the frequency of the high resolution timer.

	Returns:
		Returns the frequency of the high resolution timer.
*/
int64_t time_freq();

/*
	Function: time_timestamp
		Retrieves the current time as a UNIX timestamp

	Returns:
		The time as a UNIX timestamp
*/
int time_timestamp();

/*
	Function: time_houroftheday
		Retrieves the hours since midnight (0..23)

	Returns:
		The current hour of the day
*/
int time_houroftheday();

enum
{
	SEASON_SPRING = 0,
	SEASON_SUMMER,
	SEASON_AUTUMN,
	SEASON_WINTER,
	SEASON_NEWYEAR
};

/*
	Function: time_season
		Retrieves the current season of the year.

	Returns:
		one of the SEASON_* enum literals
*/
int time_season();

/*
	Function: time_isxmasday
		Checks if it's xmas

	Returns:
		1 - if it's a xmas day
		0 - if not
*/
int time_isxmasday();

/*
	Function: time_iseasterday
		Checks if today is in between Good Friday and Easter Monday (Gregorian calendar)

	Returns:
		1 - if it's egg time
		0 - if not
*/
int time_iseasterday();

/* Group: Network General */
typedef struct
{
	int type;
	int ipv4sock;
	int ipv6sock;
} NETSOCKET;

enum
{
	NETADDR_MAXSTRSIZE = 1 + (8 * 4 + 7) + 1 + 1 + 5 + 1, // [XXXX:XXXX:XXXX:XXXX:XXXX:XXXX:XXXX:XXXX]:XXXXX

	NETADDR_SIZE_IPV4 = 4,
	NETADDR_SIZE_IPV6 = 16,

	NETTYPE_INVALID = 0,
	NETTYPE_IPV4 = 1,
	NETTYPE_IPV6 = 2,
	NETTYPE_LINK_BROADCAST = 4,
	NETTYPE_ALL = NETTYPE_IPV4 | NETTYPE_IPV6
};

typedef struct
{
	unsigned int type;
	unsigned char ip[NETADDR_SIZE_IPV6];
	unsigned short port;
	unsigned short reserved;
} NETADDR;

/*
	Function: net_invalidate_socket
		Invalidates a socket.

	Remarks:
		You should close the socket before invalidating it.
*/
void net_invalidate_socket(NETSOCKET *socket);
/*
	Function: net_init
		Initiates network functionality.

	Returns:
		Returns 0 on success,

	Remarks:
		You must call this function before using any other network
		functions.
*/
int net_init();

/*
	Function: net_host_lookup
		Does a hostname lookup by name and fills out the passed
		NETADDR struct with the recieved details.

	Returns:
		0 on success.
*/
int net_host_lookup(const char *hostname, NETADDR *addr, int types);

/*
	Function: net_addr_comp
		Compares two network addresses.

	Parameters:
		a - Address to compare
		b - Address to compare to.
		check_port - compares port or not

	Returns:
		0 - Address a is equal to address b
		-1 - Address a differs from address b
*/
int net_addr_comp(const NETADDR *a, const NETADDR *b, int check_port);

/*
	Function: net_addr_str
		Turns a network address into a representive string.

	Parameters:
		addr - Address to turn into a string.
		string - Buffer to fill with the string.
		max_length - Maximum size of the string.
		add_port - add port to string or not

	Remarks:
		- The string will always be zero terminated

*/
void net_addr_str(const NETADDR *addr, char *string, int max_length, int add_port);

/*
	Function: net_addr_from_str
		Turns string into a network address.

	Returns:
		0 on success

	Parameters:
		addr - Address to fill in.
		string - String to parse.
*/
int net_addr_from_str(NETADDR *addr, const char *string);

/* Group: Network UDP */

/*
	Function: net_udp_create
		Creates a UDP socket and binds it to a port.

	Parameters:
		bindaddr - Address to bind the socket to.
		use_random_port - use a random port

	Returns:
		On success it returns an handle to the socket. On failure it
		returns NETSOCKET_INVALID.
*/
NETSOCKET net_udp_create(NETADDR bindaddr, int use_random_port);

/*
	Function: net_udp_send
		Sends a packet over an UDP socket.

	Parameters:
		sock - Socket to use.
		addr - Where to send the packet.
		data - Pointer to the packet data to send.
		size - Size of the packet.

	Returns:
		On success it returns the number of bytes sent. Returns -1
		on error.
*/
int net_udp_send(NETSOCKET sock, const NETADDR *addr, const void *data, int size);

/*
	Function: net_udp_recv
		Receives a packet over an UDP socket.

	Parameters:
		sock - Socket to use.
		addr - Pointer to an NETADDR that will receive the address.
		data - Pointer to a buffer that will receive the data.
		maxsize - Maximum size to receive.

	Returns:
		On success it returns the number of bytes recived. Returns -1
		on error.
*/
int net_udp_recv(NETSOCKET sock, NETADDR *addr, void *data, int maxsize);

/*
	Function: net_udp_close
		Closes an UDP socket.

	Parameters:
		sock - Socket to close.

	Returns:
		Returns 0 on success. -1 on error.
*/
int net_udp_close(NETSOCKET sock);

/* Group: Network TCP */

/*
	Function: net_tcp_create
		Creates a TCP socket.

	Parameters:
		bindaddr - Address to bind the socket to.

	Returns:
		On success it returns an handle to the socket. On failure it returns NETSOCKET_INVALID.
*/
NETSOCKET net_tcp_create(NETADDR bindaddr);

/*
	Function: net_tcp_set_linger
		Sets behaviour when closing the socket.

	Parameters:
		sock - Socket to use.
		state - What to do when closing the socket.
				1 abort connection on close.
				0 shutdown the connection properly on close.

	Returns:
		Returns 0 on success.
*/
int net_tcp_set_linger(NETSOCKET sock, int state);

/*
	Function: net_tcp_listen
		Makes the socket start listening for new connections.

	Parameters:
		sock - Socket to start listen to.
		backlog - Size of the queue of incomming connections to keep.

	Returns:
		Returns 0 on success.
*/
int net_tcp_listen(NETSOCKET sock, int backlog);

/*
	Function: net_tcp_accept
		Polls a listning socket for a new connection.

	Parameters:
		sock - Listning socket to poll.
		new_sock - Pointer to a socket to fill in with the new socket.
		addr - Pointer to an address that will be filled in the remote address (optional, can be NULL).

	Returns:
		Returns a non-negative integer on success. Negative integer on failure.
*/
int net_tcp_accept(NETSOCKET sock, NETSOCKET *new_sock, NETADDR *addr);

/*
	Function: net_tcp_connect
		Connects one socket to another.

	Parameters:
		sock - Socket to connect.
		addr - Address to connect to.

	Returns:
		Returns 0 on success.

*/
int net_tcp_connect(NETSOCKET sock, const NETADDR *addr);

/*
	Function: net_tcp_send
		Sends data to a TCP stream.

	Parameters:
		sock - Socket to send data to.
		data - Pointer to the data to send.
		size - Size of the data to send.

	Returns:
		Number of bytes sent. Negative value on failure.
*/
int net_tcp_send(NETSOCKET sock, const void *data, int size);

/*
	Function: net_tcp_recv
		Recvives data from a TCP stream.

	Parameters:
		sock - Socket to recvive data from.
		data - Pointer to a buffer to write the data to
		max_size - Maximum of data to write to the buffer.

	Returns:
		Number of bytes recvived. Negative value on failure. When in
		non-blocking mode, it returns 0 when there is no more data to
		be fetched.
*/
int net_tcp_recv(NETSOCKET sock, void *data, int maxsize);

/*
	Function: net_tcp_close
		Closes a TCP socket.

	Parameters:
		sock - Socket to close.

	Returns:
		Returns 0 on success. Negative value on failure.
*/
int net_tcp_close(NETSOCKET sock);

/* Group: Strings */

/*
	Function: str_append
		Appends a string to another.

	Parameters:
		dst - Pointer to a buffer that contains a string.
		src - String to append.
		dst_size - Size of the buffer of the dst string.

	Remarks:
		- The strings are treated as zero-terminated strings.
		- Garantees that dst string will contain zero-termination.
*/
void str_append(char *dst, const char *src, int dst_size);

/*
	Function: str_copy
		Copies a string to another.

	Parameters:
		dst - Pointer to a buffer that shall receive the string.
		src - String to be copied.
		dst_size - Size of the buffer dst.

	Remarks:
		- The strings are treated as zero-terminated strings.
		- Garantees that dst string will contain zero-termination.
*/
void str_copy(char *dst, const char *src, int dst_size);

/*
	Function: str_truncate
		Truncates a string to a given length.

	Parameters:
		dst - Pointer to a buffer that shall receive the string.
		dst_size - Size of the buffer dst.
		src - String to be truncated.
		truncation_len - Maximum length of the returned string (not
		counting the zero termination).

	Remarks:
		- The strings are treated as zero-terminated strings.
		- Garantees that dst string will contain zero-termination.
*/
void str_truncate(char *dst, int dst_size, const char *src, int truncation_len);

/*
	Function: str_length
		Returns the length of a zero terminated string.

	Parameters:
		str - Pointer to the string.

	Returns:
		Length of string in bytes excluding the zero termination.
*/
int str_length(const char *str);

/*
	Function: str_format
		Performs printf formatting into a buffer.

	Parameters:
		buffer - Pointer to the buffer to receive the formatted string.
		buffer_size - Size of the buffer.
		format - printf formatting string.
		... - Parameters for the formatting.

	Remarks:
		- See the C manual for syntax for the printf formatting string.
		- The strings are treated as zero-termineted strings.
		- Guarantees that dst string will contain zero-termination.
*/
void str_format(char *buffer, int buffer_size, const char *format, ...)
	GNUC_ATTRIBUTE((format(printf, 3, 4)));

/*
	Function: str_sanitize_strong
		Replaces all characters below 32 and above 127 with whitespace.

	Parameters:
		str - String to sanitize.

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
void str_sanitize_strong(char *str);

/*
	Function: str_sanitize_cc
		Replaces all characters below 32 with whitespace.

	Parameters:
		str - String to sanitize.

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
void str_sanitize_cc(char *str);

/*
	Function: str_sanitize
		Replaces all characters below 32 with whitespace with
		exception to \t, \n and \r.

	Parameters:
		str - String to sanitize.

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
void str_sanitize(char *str);

/*
	Function: str_sanitize_filename
		Replaces all forbidden Windows/Unix characters with whitespace
		or nothing if leading or trailing.

	Parameters:
		str - String to sanitize.

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
char *str_sanitize_filename(char *name);

/*
	Function: str_path_unsafe
		Check if the string contains '..' (parent directory) paths.

	Parameters:
		str - String to check.

	Returns:
		Returns 0 if the path is safe, -1 otherwise.

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
int str_path_unsafe(const char *str);

/*
	Function: str_clean_whitespaces
		Removes leading and trailing spaces and limits the use of multiple spaces.

	Parameters:
		str - String to clean up

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
void str_clean_whitespaces(char *str);

/*
	Function: str_clean_whitespaces_simple
		Removes leading and trailing spaces

	Parameters:
		str - String to clean up

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
void str_clean_whitespaces_simple(char *str);

/*
	Function: str_skip_to_whitespace
		Skips leading non-whitespace characters(all but ' ', '\t', '\n', '\r').

	Parameters:
		str - Pointer to the string.

	Returns:
		Pointer to the first whitespace character found
		within the string.

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
char *str_skip_to_whitespace(char *str);

/*
	Function: str_skip_to_whitespace_const
		See str_skip_to_whitespace.
*/
const char *str_skip_to_whitespace_const(const char *str);

/*
	Function: str_skip_whitespaces
		Skips leading whitespace characters(' ', '\t', '\n', '\r').

	Parameters:
		str - Pointer to the string.

	Returns:
		Pointer to the first non-whitespace character found
		within the string.

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
char *str_skip_whitespaces(char *str);

/*
	Function: str_skip_whitespaces_const
		See str_skip_whitespaces.
*/
const char *str_skip_whitespaces_const(const char *str);

/*
	Function: str_comp_nocase
		Compares to strings case insensitive.

	Parameters:
		a - String to compare.
		b - String to compare.

	Returns:
		<0 - String a is lesser then string b
		0 - String a is equal to string b
		>0 - String a is greater then string b

	Remarks:
		- Only garanted to work with a-z/A-Z.
		- The strings are treated as zero-terminated strings.
*/
int str_comp_nocase(const char *a, const char *b);

/*
	Function: str_comp_nocase_num
		Compares up to num characters of two strings case insensitive.

	Parameters:
		a - String to compare.
		b - String to compare.
		num - Maximum characters to compare

	Returns:
		<0 - String a is lesser than string b
		0 - String a is equal to string b
		>0 - String a is greater than string b

	Remarks:
		- Only garanted to work with a-z/A-Z.
		- The strings are treated as zero-terminated strings.
*/
int str_comp_nocase_num(const char *a, const char *b, const int num);

/*
	Function: str_comp
		Compares to strings case sensitive.

	Parameters:
		a - String to compare.
		b - String to compare.

	Returns:
		<0 - String a is lesser then string b
		0 - String a is equal to string b
		>0 - String a is greater then string b

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
int str_comp(const char *a, const char *b);

/*
	Function: str_comp_num
		Compares up to num characters of two strings case sensitive.

	Parameters:
		a - String to compare.
		b - String to compare.
		num - Maximum characters to compare

	Returns:
		<0 - String a is lesser then string b
		0 - String a is equal to string b
		>0 - String a is greater then string b

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
int str_comp_num(const char *a, const char *b, const int num);

/*
	Function: str_comp_filenames
		Compares two strings case sensitive, digit chars will be compared as numbers.

	Parameters:
		a - String to compare.
		b - String to compare.

	Returns:
		<0 - String a is lesser then string b
		0 - String a is equal to string b
		>0 - String a is greater then string b

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
int str_comp_filenames(const char *a, const char *b);

/*
	Function: str_startswith_nocase
		Checks case insensitive whether the string begins with a certain prefix.

	Parameter:
		str - String to check.
		prefix - Prefix to look for.

	Returns:
		A pointer to the string str after the string prefix, or 0 if
		the string prefix isn't a prefix of the string str.

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
const char *str_startswith_nocase(const char *str, const char *prefix);

/*
	Function: str_startswith
		Checks case sensitive whether the string begins with a certain prefix.

	Parameter:
		str - String to check.
		prefix - Prefix to look for.

	Returns:
		A pointer to the string str after the string prefix, or 0 if
		the string prefix isn't a prefix of the string str.

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
const char *str_startswith(const char *str, const char *prefix);

/*
	Function: str_endswith_nocase
		Checks case insensitive whether the string ends with a certain suffix.

	Parameter:
		str - String to check.
		suffix - Suffix to look for.

	Returns:
		A pointer to the beginning of the suffix in the string str, or
		0 if the string suffix isn't a suffix of the string str.

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
const char *str_endswith_nocase(const char *str, const char *suffix);

/*
	Function: str_endswith
		Checks case sensitive whether the string ends with a certain suffix.

	Parameter:
		str - String to check.
		suffix - Suffix to look for.

	Returns:
		A pointer to the beginning of the suffix in the string str, or
		0 if the string suffix isn't a suffix of the string str.

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
const char *str_endswith(const char *str, const char *suffix);

/*
	Function: str_find_nocase
		Finds a string inside another string case insensitive.

	Parameters:
		haystack - String to search in
		needle - String to search for

	Returns:
		A pointer into haystack where the needle was found.
		Returns NULL of needle could not be found.

	Remarks:
		- Only garanted to work with a-z/A-Z.
		- The strings are treated as zero-terminated strings.
*/
const char *str_find_nocase(const char *haystack, const char *needle);

/*
	Function: str_find
		Finds a string inside another string case sensitive.

	Parameters:
		haystack - String to search in
		needle - String to search for

	Returns:
		A pointer into haystack where the needle was found.
		Returns NULL of needle could not be found.

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
const char *str_find(const char *haystack, const char *needle);

/*
	Function: str_hex_decode
		Takes a hex string *without spaces between bytes* and returns a
		byte array.

	Parameters:
		dst - Buffer for the byte array
		dst_size - size of the buffer
		data - String to decode

	Returns:
		2 - String doesn't exactly fit the buffer
		1 - Invalid character in string
		0 - Success

	Remarks:
		- The contents of the buffer is only valid on success
*/
int str_hex_decode(void *dst, int dst_size, const char *src);

/*
	Function: str_hex
		Takes a datablock and generates a hexstring of it.

	Parameters:
		dst - Buffer to fill with hex data
		dst_size - size of the buffer
		data - Data to turn into hex
		data - Size of the data

	Remarks:
		- The destination buffer will be zero-terminated
*/
void str_hex(char *dst, int dst_size, const void *data, int data_size);

/*
	Function: str_is_number
		Check if the string contains only digits.

	Parameters:
		str - String to check.

	Returns:
		Returns 0 if it's a number, -1 otherwise.

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
int str_is_number(const char *pstr);

/*
	Function: str_timestamp
		Copies a time stamp in the format year-month-day_hour-minute-second to the string.

	Parameters:
		buffer - Pointer to a buffer that shall receive the time stamp string.
		buffer_size - Size of the buffer.

	Remarks:
		- Guarantees that buffer string will contain zero-termination.
*/
void str_timestamp(char *buffer, int buffer_size);
void str_timestamp_format(char *buffer, int buffer_size, const char *format)
	GNUC_ATTRIBUTE((format(strftime, 3, 0)));
void str_timestamp_ex(time_t time, char *buffer, int buffer_size, const char *format)
	GNUC_ATTRIBUTE((format(strftime, 4, 0)));

/*
	Function: str_span
		Returns the length of the minimum initial segment that doesn't contain characters in set

	Parameters:
		str - String to search in
		set - Set of characters to stop on

	Remarks:
		- Also stops on '\0'
*/
int str_span(const char *str, const char *set);

#define FORMAT_TIME "%H:%M:%S"
#define FORMAT_SPACE "%Y-%m-%d %H:%M:%S"
#define FORMAT_NOSPACE "%Y-%m-%d_%H-%M-%S"

/* Group: Filesystem */

/*
	Function: fs_listdir
		Lists the files in a directory

	Parameters:
		dir - Directory to list
		cb - Callback function to call for each entry
		type - Type of the directory
		user - Pointer to give to the callback
*/
typedef int (*FS_LISTDIR_CALLBACK)(const char *name, int is_dir, int dir_type, void *user);
void fs_listdir(const char *dir, FS_LISTDIR_CALLBACK cb, int type, void *user);

typedef struct
{
	const char *m_pName;
	time_t m_TimeCreated; // seconds since UNIX Epoch
	time_t m_TimeModified; // seconds since UNIX Epoch
} CFsFileInfo;

/*
	Function: fs_listdir_fileinfo
		Lists the files in a directory and gets additional file information

	Parameters:
		dir - Directory to list
		cb - Callback function to call for each entry
		type - Type of the directory
		user - Pointer to give to the callback
*/
typedef int (*FS_LISTDIR_CALLBACK_FILEINFO)(const CFsFileInfo *info, int is_dir, int dir_type, void *user);
void fs_listdir_fileinfo(const char *dir, FS_LISTDIR_CALLBACK_FILEINFO cb, int type, void *user);

/*
	Function: fs_makedir
		Creates a directory

	Parameters:
		path - Directory to create

	Returns:
		Returns 0 on success. Negative value on failure.

	Remarks:
		Does not create several directories if needed. "a/b/c" will result
		in a failure if b or a does not exist.
*/
int fs_makedir(const char *path);

/*
	Function: fs_makedir_recursive
		Recursively create directories

	Parameters:
		path - Path to create

	Returns:
		Returns 0 on success. Negative value on failure.
*/
int fs_makedir_recursive(const char *path);

/*
	Function: fs_storage_path
		Fetches per user configuration directory.

	Returns:
		Returns 0 on success. Negative value on failure.

	Remarks:
		- Returns ~/.appname on UNIX based systems
		- Returns ~/Library/Applications Support/appname on macOS
		- Returns %APPDATA%/Appname on Windows based systems
*/
int fs_storage_path(const char *appname, char *path, int max);

/*
	Function: fs_is_dir
		Checks if directory exists

	Returns:
		Returns 1 on success, 0 on failure.
*/
int fs_is_dir(const char *path);

/*
	Function: fs_chdir
		Changes current working directory

	Returns:
		Returns 0 on success, 1 on failure.
*/
int fs_chdir(const char *path);

/*
	Function: fs_getcwd
		Gets the current working directory.

	Parameters:
		buffer - Pointer to a buffer that will hold the result.
		buffer_size - The size of the buffer in bytes.

	Returns:
		Returns a pointer to the buffer on success, 0 on failure.
		On success, the buffer contains the result as a zero-terminated string.
		On failure, the buffer contains an empty zero-terminated string.

*/
char *fs_getcwd(char *buffer, int buffer_size);

/*
	Function: fs_parent_dir
		Get the parent directory of a directory

	Parameters:
		path - The directory string

	Returns:
		Returns 0 on success, 1 on failure.

	Remarks:
		- The string is treated as zero-terminated string.
*/
int fs_parent_dir(char *path);

/*
	Function: fs_remove
		Deletes the file with the specified name.

	Parameters:
		filename - The file to delete

	Returns:
		Returns 0 on success, 1 on failure.

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
int fs_remove(const char *filename);

/*
	Function: fs_rename
		Renames the file or directory. If the paths differ the file will be moved.

	Parameters:
		oldname - The actual name
		newname - The new name

	Returns:
		Returns 0 on success, 1 on failure.

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
int fs_rename(const char *oldname, const char *newname);

/*
	Function: fs_read
		Reads a whole file into memory and returns its contents.

	Parameters:
		name - The filename to read.
		result - Receives the file's contents.
		result_len - Receives the file's length.

	Returns:
		Returns 0 on success, 1 on failure.

	Remarks:
		- Does NOT guarantee that there are no internal null bytes.
		- Guarantees that result will contain zero-termination.
		- The result must be freed after it has been used.
*/
int fs_read(const char *name, void **result, unsigned *result_len);

/*
	Function: fs_read_str
		Reads a whole file into memory and returns its contents,
		guaranteeing a null-terminated string with no internal null
		bytes.

	Parameters:
		name - The filename to read.

	Returns:
		Returns the file's contents on success, null on failure.

	Remarks:
		- Guarantees that there are no internal null bytes.
		- Guarantees that result will contain zero-termination.
		- The result must be freed after it has been used.
*/
char *fs_read_str(const char *name);

/*
	Function: fs_file_time
		Gets the creation and the last modification date of a file.

	Parameters:
		name - The filename.
		created - Pointer to time_t
		modified - Pointer to time_t

	Returns:
		0 on success non-zero on failure

	Remarks:
		- Returned time is in seconds since UNIX Epoch
*/
int fs_file_time(const char *name, time_t *created, time_t *modified);

/*
	Group: Undocumented
*/

/*
	Function: net_tcp_connect_non_blocking

	DOCTODO: serp
*/
int net_tcp_connect_non_blocking(NETSOCKET sock, NETADDR bindaddr);

/*
	Function: net_set_non_blocking

	DOCTODO: serp
*/
int net_set_non_blocking(NETSOCKET sock);

/*
	Function: net_set_non_blocking

	DOCTODO: serp
*/
int net_set_blocking(NETSOCKET sock);

/*
	Function: net_errno

	DOCTODO: serp
*/
int net_errno();

/*
	Function: net_would_block

	DOCTODO: serp
*/
int net_would_block();

int net_socket_read_wait(NETSOCKET sock, int time);

void swap_endian(void *data, unsigned elem_size, unsigned num);

typedef void (*DBG_LOGGER)(const char *line, void *user);
typedef void (*DBG_LOGGER_FINISH)(void *user);
void dbg_logger(DBG_LOGGER logger, DBG_LOGGER_FINISH finish, void *user);

void dbg_logger_stdout();
void dbg_logger_debugger();
void dbg_logger_file(IOHANDLE logfile);

#if defined(CONF_FAMILY_WINDOWS)
void dbg_console_init();
void dbg_console_cleanup();
void dbg_console_hide();
#endif

typedef struct
{
	int sent_packets;
	int sent_bytes;
	int recv_packets;
	int recv_bytes;
} NETSTATS;

void net_stats(NETSTATS *stats);

int str_toint(const char *str);
float str_tofloat(const char *str);
int str_isspace(char c);
char str_uppercase(char c);
unsigned str_quickhash(const char *str);

enum
{
	UTF8_BYTE_LENGTH = 4
};

/*
	Function: str_next_token
		Writes the next token after str into buf, returns the rest of the string.

	Parameters:
		str - Pointer to string.
		delim - Delimiter for tokenization.
		buffer - Buffer to store token in.
		buffer_size - Size of the buffer.

	Returns:
		Pointer to rest of the string.

	Remarks:
		- The token is always null-terminated.
*/
const char *str_next_token(const char *str, const char *delim, char *buffer, int buffer_size);

/*
	Function: str_utf8_is_whitespace
		Check if the unicode is an utf8 whitespace.

	Parameters:
		code - unicode.

	Returns:
		Returns 1 on success, 0 on failure.
*/
int str_utf8_is_whitespace(int code);

/*
	Function: str_utf8_skip_whitespaces
		Skips leading utf8 whitespace characters.

	Parameters:
		str - Pointer to the string.

	Returns:
		Pointer to the first non-whitespace character found
		within the string.

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
const char *str_utf8_skip_whitespaces(const char *str);

/*
	Function: str_utf8_trim_whitespaces_right
		Clears trailing utf8 whitespace characters from a string.

	Parameters:
		str - Pointer to the string.

	Remarks:
		- The strings are treated as zero-terminated strings.
*/
void str_utf8_trim_whitespaces_right(char *str);

/*
	Function: str_utf8_rewind
		Moves a cursor backwards in an utf8 string

	Parameters:
		str - utf8 string
		cursor - position in the string

	Returns:
		New cursor position.

	Remarks:
		- Won't move the cursor less then 0
*/
int str_utf8_rewind(const char *str, int cursor);

/*
	Function: str_utf8_forward
		Moves a cursor forwards in an utf8 string

	Parameters:
		str - utf8 string
		cursor - position in the string

	Returns:
		New cursor position.

	Remarks:
		- Won't move the cursor beyond the zero termination marker
*/
int str_utf8_forward(const char *str, int cursor);

/*
	Function: str_utf8_decode
		Decodes an utf8 character

	Parameters:
		ptr - pointer to an utf8 string. this pointer will be moved forward

	Returns:
		Unicode value for the character. -1 for invalid characters and 0 for end of string.

	Remarks:
		- This function will also move the pointer forward.
*/
int str_utf8_decode(const char **ptr);

/*
	Function: str_utf8_encode
		Encode an utf8 character

	Parameters:
		ptr - Pointer to a buffer that should recive the data. Should be able to hold at least 4 bytes.

	Returns:
		Number of bytes put into the buffer.

	Remarks:
		- Does not do zero termination of the string.
*/
int str_utf8_encode(char *ptr, int chr);

/*
	Function: str_utf8_check
		Checks if a strings contains just valid utf8 characters.

	Parameters:
		str - Pointer to a possible utf8 string.

	Returns:
		0 - invalid characters found.
		1 - only valid characters found.

	Remarks:
		- The string is treated as zero-terminated utf8 string.
*/
int str_utf8_check(const char *str);

/*
	Function: str_utf8_copy_num
		Copies a number of utf8 characters from one string to another.

	Parameters:
		dst - Pointer to a buffer that shall receive the string.
		src - String to be copied.
		dst_size - Size of the buffer dst.
		num - maximum number of utf8 characters to be copied.

	Remarks:
		- The strings are treated as zero-terminated strings.
		- Garantees that dst string will contain zero-termination.
*/
void str_utf8_copy_num(char *dst, const char *src, int dst_size, int num);

/*
	Function: str_utf8_stats
		Determines the byte size and utf8 character count of a utf8 string.

	Parameters:
		str - Pointer to the string.
		max_size - Maximum number of bytes to count.
		max_count - Maximum number of utf8 characters to count.
		size - Pointer to store size (number of non-zero bytes) of the string.
		count - Pointer to store count of utf8 characters of the string.

	Remarks:
		- The string is treated as zero-terminated utf8 string.
		- It's the user's responsibility to make sure the bounds are aligned.
*/
void str_utf8_stats(const char *str, int max_size, int max_count, int *size, int *count);

/*
	Function: secure_random_init
		Initializes the secure random module.
		You *MUST* check the return value of this function.

	Returns:
		0 - Initialization succeeded.
		1 - Initialization failed.
*/
int secure_random_init();

/*
	Function: secure_random_uninit
		Uninitializes the secure random module.

	Returns:
		0 - Uninitialization succeeded.
		1 - Uninitialization failed.
*/
int secure_random_uninit();

/*
	Function: secure_random_fill
		Fills the buffer with the specified amount of random bytes.

	Parameters:
		bytes - Pointer to the start of the buffer.
		length - Length of the buffer.
*/
void secure_random_fill(void *bytes, unsigned length);

/*
	Function: pid
		Gets the process ID of the current process

	Returns:
		The process ID of the current process.
*/
int pid();

/*
	Function: cmdline_fix
		Fixes the command line arguments to be encoded in UTF-8 on all
		systems.

	Parameters:
		argc - A pointer to the argc parameter that was passed to the main function.
		argv - A pointer to the argv parameter that was passed to the main function.

	Remarks:
		- You need to call cmdline_free once you're no longer using the
		results.
*/
void cmdline_fix(int *argc, const char ***argv);

/*
	Function: cmdline_free
		Frees memory that was allocated by cmdline_fix.

	Parameters:
		argc - The argc obtained from cmdline_fix.
		argv - The argv obtained from cmdline_fix.

*/
void cmdline_free(int argc, const char **argv);

/*
	Function: bytes_be_to_int
		Packs 4 big endian bytes into an int

	Returns:
		The packed int

	Remarks:
		- Assumes the passed array is 4 bytes
		- Assumes int is 4 bytes
*/
int bytes_be_to_int(const unsigned char *bytes);

/*
	Function: int_to_bytes_be
		Packs an int into 4 big endian bytes

	Remarks:
		- Assumes the passed array is 4 bytes
		- Assumes int is 4 bytes
*/
void int_to_bytes_be(unsigned char *bytes, int value);

/*
	Function: bytes_be_to_uint
		Packs 4 big endian bytes into an unsigned

	Returns:
		The packed unsigned

	Remarks:
		- Assumes the passed array is 4 bytes
		- Assumes unsigned is 4 bytes
*/
unsigned bytes_be_to_uint(const unsigned char *bytes);

/*
	Function: uint_to_bytes_be
		Packs an unsigned into 4 big endian bytes

	Remarks:
		- Assumes the passed array is 4 bytes
		- Assumes unsigned is 4 bytes
*/
void uint_to_bytes_be(unsigned char *bytes, unsigned value);

#endif
