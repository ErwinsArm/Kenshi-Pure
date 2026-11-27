// The versioned C interface between KenshiPure.dll (VC++ 2010, Kenshi's CRT) and KenshiPureCore.dll
// (modern MSVC, static CRT: protocol codec, ENet, the network thread). Plain C and POD only: no STL,
// no exceptions, no game pointers, no memory allocated on one side and freed on the other.
// KenshiPure.dll loads the core with LoadLibrary and asks for the table with kpCoreGetApi.
// Every function may be called from any one thread at a time per core (the frame thread in game);
// each holds the core's lock only briefly and never waits for the network.
#ifndef KP_CORE_H
#define KP_CORE_H

#define KP_CORE_VERSION 1

#define KP_CORE_HOST_MAX 128
#define KP_CORE_NAME_MAX 33         /* 32 bytes + terminator (protocol limit) */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KpCore KpCore;

/* Called from the core's network thread (and from start/stop); the line has no newline. */
typedef void (*KpCoreLogFn)(void* ctx, const char* line);

typedef struct KpCoreConfig
{
    unsigned int size;              /* sizeof(KpCoreConfig) */
    unsigned int version;           /* KP_CORE_VERSION */
    char host[KP_CORE_HOST_MAX];    /* server name or address, non-empty */
    unsigned short port;            /* 1-65535 */
    char name[KP_CORE_NAME_MAX];    /* player name, 1-32 bytes, no control bytes */
    KpCoreLogFn log;                /* may be NULL */
    void* logCtx;
} KpCoreConfig;

#define KP_POSE_MOVING 0x01
#define KP_POSE_PAUSED 0x02

typedef struct KpPose
{
    float pos[3];                   /* world units */
    float vel[3];                   /* world units per (game) second */
    float yaw;                      /* degrees, 0 = +z, 90 = +x */
    unsigned int flags;             /* KP_POSE_* */
} KpPose;

typedef enum KpEventKind
{
    KP_EV_CONNECTING = 1,           /* a connection attempt starts (a = attempt number) */
    KP_EV_WELCOME = 2,              /* a = own player id */
    KP_EV_REJECTED = 3,             /* a = reject reason (protocol) */
    KP_EV_JOINED = 4,               /* a = player id, name */
    KP_EV_LEFT = 5,                 /* a = player id, b = leave reason (protocol) */
    KP_EV_LOST = 6,                 /* connection lost after Welcome (ENet timeout or server gone) */
    KP_EV_CONNECT_FAILED = 7        /* no Welcome within the deadline, or the server refused */
} KpEventKind;

typedef struct KpEvent
{
    int kind;                       /* KpEventKind */
    unsigned int a, b;
    char name[KP_CORE_NAME_MAX];
} KpEvent;

typedef struct KpRemotePose
{
    unsigned short playerId;
    unsigned int ageMs;             /* server's age of the pose + time since it arrived here */
    unsigned int tick;              /* server snapshot tick it came in */
    KpPose pose;
} KpRemotePose;

typedef enum KpCoreState
{
    KP_CORE_WAITING = 0,            /* between attempts */
    KP_CORE_CONNECTING = 1,
    KP_CORE_WELCOMED = 2,
    KP_CORE_STOPPED = 3
} KpCoreState;

typedef struct KpCoreStatus
{
    int state;                      /* KpCoreState */
    unsigned int playerId;          /* 0 unless welcomed */
    unsigned int attempts;          /* connection attempts so far */
    unsigned int posesSent;
    unsigned int eventsDropped;     /* event queue overflow */
    unsigned int messagesDropped;   /* malformed or unexpected messages from the server */
} KpCoreStatus;

typedef struct KpCoreApi
{
    unsigned int size;              /* sizeof(KpCoreApi) */
    unsigned int version;           /* KP_CORE_VERSION */
    const char* build;              /* e.g. "KenshiPureCore 0.0.4-m3 (built ...)" */
    /* Checks the config, starts the network thread. NULL (and a log line) if the config is bad. */
    KpCore* (*start)(const KpCoreConfig* cfg);
    /* Leaves the server (waits at most ~300 ms), stops and joins the thread, frees the core. */
    void (*stop)(KpCore* core);
    /* Latest local pose; the network thread sends each new one once while welcomed. 0 if invalid. */
    int (*setLocalPose)(KpCore* core, const KpPose* pose);
    /* Copies up to max queued events (oldest first) and removes them; returns the count. */
    int (*takeEvents)(KpCore* core, KpEvent* out, int max);
    /* Copies up to max remote poses that changed since the last call; returns the count. */
    int (*takeRemotePoses)(KpCore* core, KpRemotePose* out, int max);
    void (*status)(KpCore* core, KpCoreStatus* out);
} KpCoreApi;

/* The core DLL's only export. NULL if it does not speak this version. */
typedef const KpCoreApi* (*KpCoreGetApiFn)(unsigned int version);

#ifdef __cplusplus
}
#endif

#endif
