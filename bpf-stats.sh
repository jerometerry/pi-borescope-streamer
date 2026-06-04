#!/usr/bin/env bpftrace

BEGIN
{
    printf("Monitoring pi-borescope-streamer... Hit Ctrl-C to exit.\n");
    printf("=================================================================\n");
    printf("%-12s %-15s %-15s %-15s\n", "TIME", "MALLOCS/sec", "FREES/sec", "NET_SENDS/sec");
    printf("=================================================================\n");
}

/* * 1. Proving Zero Memory Allocation
 * Target all dynamic memory allocation calls in user-space.
 * (Note: Linux truncates process 'comm' names to 15 characters, 
 * so "v4l2-borescope-daemon" becomes "v4l2-borescope-")
 */
uprobe:libc:malloc,
uprobe:libc:calloc,
uprobe:libc:realloc
/comm == "v4l2-borescope-"/
{
    @allocs++;
}

uprobe:libc:free
/comm == "v4l2-borescope-"/
{
    @frees++;
}

/* * 2. Proving High Performance / Throughput
 * Target the network socket writes from your custom uWebSockets server.
 */
tracepoint:syscalls:sys_enter_sendto,
tracepoint:syscalls:sys_enter_writev,
tracepoint:syscalls:sys_enter_sendmsg
/comm == "v4l2-borescope-"/
{
    @sends++;
}

/* * 3. The Dashboard Ticker
 * Flush the counts to the screen every 1 second.
 */
interval:s:1
{
    time("%H:%M:%S    ");
    printf("%-15d %-15d %-15d\n", @allocs, @frees, @sends);
    
    // Reset counters for the next second
    clear(@allocs);
    clear(@frees);
    clear(@sends);
}