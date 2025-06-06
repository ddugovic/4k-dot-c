#pragma region libc shims

#ifdef _MSC_VER
#define __attribute__(...)
#endif

#if defined(_WIN64) || defined(__x86_64__) || defined(__ppc64__) ||            \
    defined(__aarch64__)
#define ARCH64 1
#ifdef NOSTDLIB
#define size_t unsigned long long
#define ssize_t long long
#endif
#else
#define ARCH32 1
#ifdef NOSTDLIB
#define size_t unsigned int
#define ssize_t int
#endif
#endif

#define i64 long long
#define u64 unsigned long long
#define i32 int
#define u32 unsigned
#define i16 short
#define u16 unsigned short
#define i8 signed char
#define u8 unsigned char

#ifdef NOSTDLIB

#if __GNUC__ < 13
typedef _Bool bool;
#define true 1
#define false 0
#endif

#define NULL ((void *)0)

enum [[nodiscard]] {
  stdin = 0,
  stdout = 1,
  stderr = 2,
};

static ssize_t _sys(ssize_t call, ssize_t arg1, ssize_t arg2, ssize_t arg3) {
  ssize_t ret;
#ifdef ARCH64
  asm volatile("syscall"
               : "=a"(ret)
               : "a"(call), "D"(arg1), "S"(arg2), "d"(arg3)
               : "rcx", "r11", "memory");
#else
  asm volatile("int $0x80"
               : "=a"(ret)
               : "a"(call), "b"(arg1), "c"(arg2), "d"(arg3)
               : "memory");
#endif
  return ret;
}

static void exit_now() {
#ifdef ARCH32
  asm volatile("movl $1, %eax\n\t"
               "int $0x80");
#else
  asm volatile("movl $60, %eax\n\t"
               "syscall");
#endif
}

[[nodiscard]] static i32 strlen(const char *const restrict string) {
  i32 length = 0;
  while (string[length]) {
    length++;
  }
  return length;
}

static void putl(const char *const restrict string) {
  i32 length = 0;
  while (string[length]) {
#ifdef ARCH64
    _sys(1, stdout, (ssize_t)(&string[length]), 1);
#else
    _sys(4, stdout, (ssize_t)(&string[length]), 1);
#endif
    length++;
  }
}

// Non-standard, gets but a word instead of a line
static bool getl(char *restrict string) {
  while (true) {
#ifdef ARCH64
    const int result = _sys(0, stdin, (ssize_t)string, 1);
#else
    const int result = _sys(3, stdin, (ssize_t)string, 1);
#endif

    // Assume stdin never closes on mini build
#ifdef FULL
    if (result < 1) {
      exit_now();
    }
#endif

    const char ch = *string;
    if (ch == '\n' || ch == ' ') {
      *string = 0;
      return ch != '\n';
    }

    string++;
  }
}

[[nodiscard]] static bool strcmp(const char *restrict lhs,
                                 const char *restrict rhs) {
  while (*lhs || *rhs) {
    if (*lhs != *rhs) {
      return true;
    }
    lhs++;
    rhs++;
  }
  return false;
}

[[nodiscard]] static u32 atoi(const char *restrict string) {
  // Will break if reads a value over 4294967295
  // This works out to be just over 49 days

  u32 result = 0;
  while (true) {
    if (!*string) {
      return result;
    }
    result *= 10;
    result += *string - '0';
    string++;
  }
}

#define printf(format, ...) _printf(format, (size_t[]){__VA_ARGS__})

static void _printf(const char *format, const size_t *args) {
  long long value;
  char buffer[16], *string;

  while (true) {
    if (!*format) {
      break;
    }
    if (*format != '%') {
#ifdef ARCH64
      _sys(1, stdout, (ssize_t)format, 1);
#else
      _sys(4, stdout, (ssize_t)format, 1);
#endif
      format++;
      continue;
    }

    format++;
    switch (*format++) {
    case 's':
      putl((char *)*args);
      break;
    case 'i':
      value = *args;
      if (value < 0) {
        putl("-");
        value *= -1;
      }
      string = buffer + sizeof buffer - 1;
      *string-- = 0;
      for (;;) {
        *string = '0' + value % 10;
        value /= 10;
        if (!value) {
          break;
        }
        string--;
      }
      putl(string);
      break;
    }
    args++;
  }
}

typedef struct [[nodiscard]] {
  ssize_t tv_sec;  // seconds
  ssize_t tv_nsec; // nanoseconds
} timespec;

[[nodiscard]] static size_t get_time() {
  timespec ts;
#ifdef ARCH64
  _sys(228, 1, (ssize_t)&ts, 0);
#else
  _sys(265, 1, (ssize_t)&ts, 0);
#endif
  return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

#ifdef ASSERTS
#define assert(condition)                                                      \
  if (!(condition)) {                                                          \
    printf("Assert failed on line %i: ", __LINE__);                            \
    putl(#condition "\n");                                                     \
    _sys(60, 1, 0, 0);                                                         \
  }
#else
#define assert(condition)
#endif

#else
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

[[nodiscard]] static size_t get_time() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void exit_now() { exit(0); }

static bool getl(char *restrict string) {
  while (true) {

    const char c = getchar();

    if (c == EOF) {
      exit_now();
    }

    if (c == '\n') {
      *string = 0;
      return false;
    }

    if (c == ' ') {
      *string = 0;
      return true;
    }

    *string = c;
    string++;
  }
}

static void putl(const char *const restrict string) {
  fputs(string, stdout);
  fflush(stdout);
}

#endif

#pragma endregion

#pragma region base

enum [[nodiscard]] { None, Pawn, Knight, Bishop, Rook, Queen, King };

typedef struct [[nodiscard]] {
  u8 promo;
  u8 from;
  u8 to;
  u8 takes_piece;
} Move;

typedef struct [[nodiscard]] {
  u64 pieces[7];
  u64 colour[2];
  u64 ep;
  bool castling[4];
  bool flipped;
} Position;

[[nodiscard]] static bool move_string_equal(const char *restrict lhs,
                                            const char *restrict rhs) {
  return (*(const u64 *)lhs ^ *(const u64 *)rhs) << 24 == 0;
}

[[nodiscard]] static u64 flip_bb(const u64 bb) { return __builtin_bswap64(bb); }

#ifdef ARCH32
#pragma GCC push_options
#pragma GCC optimize("O3")
#endif
static i32 lsb(u64 bb) { return __builtin_ctzll(bb); }
#ifdef ARCH32
#pragma GCC pop_options
#endif

[[nodiscard]] static i32 count(const u64 bb) {
  return __builtin_popcountll(bb);
}

[[nodiscard]] static u64 shift(const u64 bb, const i32 shift, const u64 mask) {
  return shift > 0 ? bb << shift & mask : bb >> -shift & mask;
}

[[nodiscard]] static u64 west(const u64 bb) {
  return bb >> 1 & ~0x8080808080808080ull;
}

[[nodiscard]] static u64 east(const u64 bb) {
  return bb << 1 & ~0x101010101010101ull;
}

[[nodiscard]] static u64 north(const u64 bb) { return bb << 8; }

[[nodiscard]] static u64 south(const u64 bb) { return bb >> 8; }

[[nodiscard]] static u64 nw(const u64 bb) {
  return shift(bb, 7, ~0x8080808080808080ull);
  // return west(north(bb));
}

[[nodiscard]] static u64 ne(const u64 bb) { return east(north(bb)); }

[[nodiscard]] static u64 sw(const u64 bb) { return west(south(bb)); }

[[nodiscard]] static u64 se(const u64 bb) { return east(south(bb)); }

[[nodiscard]] u64 xattack(const i32 sq, const u64 blockers,
                          const u64 dir_mask) {
  return dir_mask &
         ((blockers & dir_mask) - (1ULL << sq) ^
          flip_bb(flip_bb(blockers & dir_mask) - flip_bb(1ULL << sq)));
}

[[nodiscard]] static u64 ray(const i32 sq, const u64 blockers,
                             const i32 shift_by, const u64 mask) {
  assert(sq >= 0);
  assert(sq < 64);
  u64 result = shift(1ull << sq, shift_by, mask);
  for (i32 i = 0; i < 6; i++) {
    result |= shift(result & ~blockers, shift_by, mask);
  }
  return result;
}

static u64 diag_mask[64];

static void init_diag_masks() {
  for (i32 sq = 0; sq < 64; sq++) {
    diag_mask[sq] = ray(sq, 0, 9, ~0x101010101010101ull) |  // Northeast
                    ray(sq, 0, -9, ~0x8080808080808080ull); // Southwest
  }
}

[[nodiscard]] static u64 bishop(const i32 sq, const u64 blockers) {
  assert(sq >= 0);
  assert(sq < 64);

  return xattack(sq, blockers, diag_mask[sq]) |
         xattack(sq, blockers, flip_bb(diag_mask[sq ^ 56]));
}

[[nodiscard]] static u64 rook(const i32 sq, const u64 blockers) {
  assert(sq >= 0);
  assert(sq < 64);
  return xattack(sq, blockers, 1ULL << sq ^ 0x101010101010101ULL << sq % 8) |
         ray(sq, blockers, 1, ~0x101010101010101ull)      // East
         | ray(sq, blockers, -1, ~0x8080808080808080ull); // West
}

[[nodiscard]] static u64 knight(const i32 sq) {
  assert(sq >= 0);
  assert(sq < 64);
  const u64 bb = 1ull << sq;
  return (bb << 15 | bb >> 17) & ~0x8080808080808080ull |
         (bb << 17 | bb >> 15) & ~0x101010101010101ull |
         (bb << 10 | bb >> 6) & 0xFCFCFCFCFCFCFCFCull |
         (bb << 6 | bb >> 10) & 0x3F3F3F3F3F3F3F3Full;
}

[[nodiscard]] static u64 king(const i32 sq) {
  assert(sq >= 0);
  assert(sq < 64);
  const u64 bb = 1ull << sq;
  return bb << 8 | bb >> 8 |
         (bb >> 1 | bb >> 9 | bb << 7) & ~0x8080808080808080ull |
         (bb << 1 | bb << 9 | bb >> 7) & ~0x101010101010101ull;
}

static void move_str(char *restrict str, const Move *restrict move,
                     const i32 flip) {
  assert(move->from >= 0);
  assert(move->from < 64);
  assert(move->to >= 0);
  assert(move->to < 64);
  assert(move->from != move->to);
  assert(move->promo == None || move->promo == Knight ||
         move->promo == Bishop || move->promo == Rook || move->promo == Queen);

  // Hack to save bytes, technically UB but works on GCC 14.2
  for (i32 i = 0; i < 2; i++) {
    str[i * 2] = 'a' + (&move->from)[i] % 8;
    str[i * 2 + 1] = '1' + ((&move->from)[i] / 8 ^ 7 * flip);
  }

  str[4] = "\0\0nbrq"[move->promo];
  str[5] = '\0';
}

[[nodiscard]] static i32 piece_on(const Position *const restrict pos,
                                  const i32 sq) {
  assert(sq >= 0);
  assert(sq < 64);
  for (i32 i = Pawn; i <= King; ++i) {
    if (pos->pieces[i] & 1ull << sq) {
      return i;
    }
  }
  return None;
}

static void swapu64(u64 *const lhs, u64 *const rhs) {
  const u64 temp = *lhs;
  *lhs = *rhs;
  *rhs = temp;
}

static void swapu32(u32 *const lhs, u32 *const rhs) {
  const u32 temp = *lhs;
  *lhs = *rhs;
  *rhs = temp;
}

static void swapmoves(Move *const lhs, Move *const rhs) {
  swapu32((u32 *)lhs, (u32 *)rhs);
}

static void swapbool(bool *const restrict lhs, bool *const restrict rhs) {
  const bool temp = *lhs;
  *lhs = *rhs;
  *rhs = temp;
}

[[nodiscard]] static bool move_equal(Move *const lhs, Move *const rhs) {
  return *(u32 *)lhs == *(u32 *)rhs;
}

static void flip_pos(Position *const restrict pos) {
  // Hack to flip the first 10 bitboards in Position.
  // Technically UB but works in GCC 14.2
  u64 *pos_ptr = (u64 *)pos;
  for (i32 i = 0; i < 10; i++) {
    pos_ptr[i] = flip_bb(pos_ptr[i]);
  }

  pos->flipped ^= 1;
  for (i32 i = 0; i < 2; i++) {
    swapbool(&pos->castling[i], &pos->castling[i + 2]);
  }
  swapu64(&pos->colour[0], &pos->colour[1]);
}

[[nodiscard]] static u64 get_mobility(const i32 sq, const i32 piece,
                                      const Position *pos) {
  u64 moves = 0;
  if (piece == Knight) {
    moves = knight(sq);
  } else if (piece == King) {
    moves = king(sq);
  } else {
    const u64 blockers = pos->colour[0] | pos->colour[1];
    if (piece == Rook || piece == Queen) {
      moves |= rook(sq, blockers);
    }
    if (piece == Bishop || piece == Queen) {
      moves |= bishop(sq, blockers);
    }
  }
  return moves;
}

[[nodiscard]] static i32 is_attacked(const Position *const restrict pos,
                                     const i32 sq, const i32 them) {
  assert(sq >= 0);
  assert(sq < 64);
  const u64 bb = 1ull << sq;
  const u64 theirs = pos->colour[them];
  const u64 pawns = theirs & pos->pieces[Pawn];
  if ((them ? sw(pawns) | se(pawns) : nw(pawns) | ne(pawns)) & bb) {
    return true;
  }
  const u64 blockers = pos->colour[0] | pos->colour[1];
  return knight(sq) & theirs & pos->pieces[Knight] ||
         bishop(sq, blockers) & theirs &
             (pos->pieces[Bishop] | pos->pieces[Queen]) ||
         rook(sq, blockers) & theirs &
             (pos->pieces[Rook] | pos->pieces[Queen]) ||
         king(sq) & theirs & pos->pieces[King];
}

i32 makemove(Position *const restrict pos, const Move *const restrict move) {
  assert(move->from >= 0);
  assert(move->from < 64);
  assert(move->to >= 0);
  assert(move->to < 64);
  assert(move->from != move->to);
  assert(move->promo == None || move->promo == Knight ||
         move->promo == Bishop || move->promo == Rook || move->promo == Queen);

  const u64 from = 1ull << move->from;
  const u64 to = 1ull << move->to;
  const u64 mask = from | to;

  assert(move->takes_piece != King);
  assert(move->takes_piece == piece_on(pos, move->to));
  const i32 piece = piece_on(pos, move->from);
  assert(piece != None);

  // Captures
  if (move->takes_piece != None) {
    pos->colour[1] ^= to;
    pos->pieces[move->takes_piece] ^= to;
  }

  // Castling
  if (piece == King) {
    const u64 bb = move->to - move->from == 2   ? 0xa0
                   : move->from - move->to == 2 ? 0x9
                                                : 0;
    pos->colour[0] ^= bb;
    pos->pieces[Rook] ^= bb;
  }

  // Move the piece
  pos->colour[0] ^= mask;
  pos->pieces[piece] ^= mask;

  // En passant
  if (piece == Pawn && to == pos->ep) {
    pos->colour[1] ^= to >> 8;
    pos->pieces[Pawn] ^= to >> 8;
  }
  pos->ep = 0;

  // Pawn double move
  if (piece == Pawn && move->to - move->from == 16) {
    pos->ep = to >> 8;
  }

  // Promotions
  if (move->promo != None) {
    pos->pieces[Pawn] ^= to;
    pos->pieces[move->promo] ^= to;
  }

  // Update castling permissions
  pos->castling[2] &= !(mask & 0x9000000000000000ull);
  pos->castling[3] &= !(mask & 0x1100000000000000ull);
  pos->castling[0] &= !(mask & 0x90ull);
  pos->castling[1] &= !(mask & 0x11ull);

  flip_pos(pos);

  assert(!(pos->colour[0] & pos->colour[1]));
  assert(!(pos->pieces[Pawn] & pos->pieces[Knight]));
  assert(!(pos->pieces[Pawn] & pos->pieces[Bishop]));
  assert(!(pos->pieces[Pawn] & pos->pieces[Rook]));
  assert(!(pos->pieces[Pawn] & pos->pieces[Queen]));
  assert(!(pos->pieces[Pawn] & pos->pieces[King]));
  assert(!(pos->pieces[Knight] & pos->pieces[Bishop]));
  assert(!(pos->pieces[Knight] & pos->pieces[Rook]));
  assert(!(pos->pieces[Knight] & pos->pieces[Queen]));
  assert(!(pos->pieces[Knight] & pos->pieces[King]));
  assert(!(pos->pieces[Bishop] & pos->pieces[Rook]));
  assert(!(pos->pieces[Bishop] & pos->pieces[Queen]));
  assert(!(pos->pieces[Bishop] & pos->pieces[King]));
  assert(!(pos->pieces[Rook] & pos->pieces[Queen]));
  assert(!(pos->pieces[Rook] & pos->pieces[King]));
  assert(!(pos->pieces[Queen] & pos->pieces[King]));

  // Return move legality
  return !is_attacked(pos, lsb(pos->colour[1] & pos->pieces[King]), false);
}

static Move *generate_pawn_moves(const Position *const pos,
                                 Move *restrict movelist, u64 to_mask,
                                 const i32 offset

) {
  while (to_mask) {
    const u8 to = lsb(to_mask);
    to_mask &= to_mask - 1;
    const u8 from = to + offset;
    assert(from >= 0);
    assert(from < 64);
    assert(to >= 0);
    assert(to < 64);
    assert(piece_on(pos, from) == Pawn);
    const u8 takes = piece_on(pos, to);
    if (to > 55) {
      for (u8 piece = Queen; piece >= Knight; piece--) {
        *movelist++ = (Move){
            .from = from, .to = to, .promo = piece, .takes_piece = takes};
      }
    } else {
      *movelist++ =
          (Move){.from = from, .to = to, .promo = None, .takes_piece = takes};
    }
  }

  return movelist;
}

static Move *generate_piece_moves(Move *restrict movelist,
                                  const Position *restrict pos,
                                  const u64 to_mask) {
  for (i32 piece = Knight; piece <= King; piece++) {
    assert(piece == Knight || piece == Bishop || piece == Rook ||
           piece == Queen || piece == King);
    u64 copy = pos->colour[0] & pos->pieces[piece];
    while (copy) {
      const u8 from = lsb(copy);
      assert(from >= 0);
      assert(from < 64);
      copy &= copy - 1;

      u64 moves = get_mobility(from, piece, pos);
      moves &= to_mask;

      while (moves) {
        const u8 to = lsb(moves);
        assert(to >= 0);
        assert(to < 64);
        moves &= moves - 1;
        *movelist++ = (Move){.from = from,
                             .to = to,
                             .promo = None,
                             .takes_piece = piece_on(pos, to)};
      }
    }
  }

  return movelist;
}

enum { max_moves = 218 };

[[nodiscard]] static i32 movegen(const Position *const restrict pos,
                                 Move *restrict movelist,
                                 const i32 only_captures) {

  const Move *start = movelist;
  const u64 all = pos->colour[0] | pos->colour[1];
  const u64 to_mask = only_captures ? pos->colour[1] : ~pos->colour[0];
  if (!only_captures) {
    movelist = generate_pawn_moves(
        pos, movelist,
        north(north(pos->colour[0] & pos->pieces[Pawn] & 0xFF00) & ~all) & ~all,
        -16);
  }
  movelist =
      generate_pawn_moves(pos, movelist,
                          north(pos->colour[0] & pos->pieces[Pawn]) & ~all &
                              (only_captures ? 0xFF00000000000000ull : ~0ull),
                          -8);
  movelist = generate_pawn_moves(
      pos, movelist,
      nw(pos->colour[0] & pos->pieces[Pawn]) & (pos->colour[1] | pos->ep), -7);
  movelist = generate_pawn_moves(
      pos, movelist,
      ne(pos->colour[0] & pos->pieces[Pawn]) & (pos->colour[1] | pos->ep), -9);
  if (pos->castling[0] && !(all & 0x60ull) && !is_attacked(pos, 4, true) &&
      !is_attacked(pos, 5, true)) {
    *movelist++ =
        (Move){.from = 4, .to = 6, .promo = None, .takes_piece = None};
  }
  if (pos->castling[1] && !(all & 0xEull) && !is_attacked(pos, 4, true) &&
      !is_attacked(pos, 3, true)) {
    *movelist++ =
        (Move){.from = 4, .to = 2, .promo = None, .takes_piece = None};
  }
  movelist = generate_piece_moves(movelist, pos, to_mask);

  const i32 num_moves = movelist - start;
  assert(num_moves < max_moves);
  return num_moves;
}

#pragma endregion

#pragma region engine

[[nodiscard]] static u64 perft(const Position *const restrict pos,
                               const i32 depth) {
  if (depth == 0) {
    return 1;
  }

  u64 nodes = 0;
  Move moves[max_moves];
  const i32 num_moves = movegen(pos, moves, false);

  for (i32 i = 0; i < num_moves; ++i) {
    Position npos = *pos;

    // Check move legality
    if (!makemove(&npos, &moves[i])) {
      continue;
    }

    nodes += perft(&npos, depth - 1);
  }

  return nodes;
}

__attribute__((aligned(8))) static const i16 material[] = {78,  308, 319,
                                                           483, 966, 0};
__attribute__((aligned(8))) static const i8 pst_rank[] = {
    0,   -12, -14, -13, -1, 40, 114, 0,   // Pawn
    -36, -19, 1,   16,  28, 28, 8,   -25, // Knight
    -27, -9,  3,   10,  15, 15, 3,   -9,  // Bishop
    -11, -19, -19, -9,  6,  15, 21,  16,  // Rook
    -21, -13, -9,  -3,  6,  16, 6,   16,  // Queen
    -20, -12, -5,  6,   18, 24, 13,  -15, // King
};
__attribute__((aligned(8))) static const i8 pst_file[] = {
    -2,  2,  -5, -2, 0,  5,  10, -8,  // Pawn
    -28, -7, 6,  15, 14, 13, 1,  -14, // Knight
    -13, 0,  3,  5,  6,  1,  5,  -7,  // Bishop
    -2,  0,  3,  5,  4,  6,  -2, -14, // Rook
    -22, -9, 2,  6,  5,  6,  6,  6,   // Queen
    -13, 3,  1,  0,  -2, -2, 6,  -10, // King
};
__attribute__((aligned(8))) static const i8 open_files[] = {27, -11, -7,
                                                            25, 5,   -7};
const i8 bishop_pair = 35;

static i32 eval(Position *const restrict pos) {
  i32 score = 16;
  for (i32 c = 0; c < 2; c++) {

    // BISHOP PAIR
    if (count(pos->colour[0] & pos->pieces[Bishop]) > 1) {
      score += bishop_pair;
    }

    const u64 own_pawns = pos->colour[0] & pos->pieces[Pawn];

    for (i32 p = Pawn; p <= King; p++) {
      u64 copy = pos->colour[0] & pos->pieces[p];
      while (copy) {
        const i32 sq = lsb(copy);
        copy &= copy - 1;

        const int rank = sq >> 3;
        const int file = sq & 7;

        // OPEN FILES / DOUBLED PAWNS
        score += open_files[p - 1] *
                 ((north(0x101010101010101ULL << sq) & own_pawns) == 0);

        // MATERIAL
        score += material[p - 1];

        // SPLIT PIECE-SQUARE TABLES
        score += pst_rank[(p - 1) * 8 + rank];
        score += pst_file[(p - 1) * 8 + file];
      }
    }

    flip_pos(pos);
    score = -score;
  }
  return score;
}

enum { max_ply = 96 };
enum { mate = 30000, inf = 32000 };

static size_t start_time;
static size_t max_time;

typedef struct [[nodiscard]] {
  i32 num_moves;
  u64 position_hash;
  Move best_move;
  Move killer;
  Move moves[max_moves];
} SearchStack;

typedef struct [[nodiscard]] __attribute__((packed)) {
  Move move;
  u16 partial_hash;
  i16 score;
  i8 depth;
  u8 flag;
} TTEntry;

enum { tt_length = 64 * 1024 * 1024 / sizeof(TTEntry) };

enum { Upper = 0, Lower = 1, Exact = 2 };

static TTEntry tt[tt_length];
static i32 move_history[2][6][64][64];

#if defined(__x86_64__) || defined(_M_X64)
typedef long long __attribute__((__vector_size__(16))) i128;

[[nodiscard]] __attribute__((target("aes"))) u64
get_hash(const Position *const pos) {
  i128 hash = {0};

  // USE 16 BYTE POSITION SEGMENTS AS KEYS FOR AES
  const u8 *const data = (const u8 *)pos;
  for (i32 i = 0; i < 5; i++) {
    i128 key;
    __builtin_memcpy(&key, data + i * 16, 16);
    hash = __builtin_ia32_aesenc128(hash, key);
  }

  // FINAL ROUND FOR BIT MIXING
  hash = __builtin_ia32_aesenc128(hash, hash);

  // USE FIRST 64 BITS AS POSITION HASH
  return hash[0];
}
#elif defined(__aarch64__)

#include <arm_neon.h>

[[nodiscard]] __attribute__((target("+aes"))) u64
get_hash(const Position *const pos) {
  uint8x16_t hash = vdupq_n_u8(0);

  // USE 16 BYTE POSITION SEGMENTS AS KEYS FOR AES
  const u8 *const data = (const u8 *)pos;
  for (i32 i = 0; i < 5; ++i) {
    uint8x16_t key;
    memcpy(&key, data + i * 16, 16);

    hash = vaesmcq_u8(vaeseq_u8(hash, vdupq_n_u8(0)));
    hash = veorq_u8(hash, key);
  }

  // FINAL ROUND FOR BIT MIXING
  uint8x16_t key = hash;
  hash = vaesmcq_u8(vaeseq_u8(hash, vdupq_n_u8(0)));
  hash = veorq_u8(hash, key);

  // USE FIRST 64 BITS AS POSITION HASH
  u64 result;
  memcpy(&result, &hash, sizeof(result));
  return result;
}

#else
#error "Unsupported architecture: get_hash only for x86_64 and aarch64"
#endif

static i16 search(Position *const restrict pos, const i32 ply, i32 depth,
                  i32 alpha, const i32 beta,
#ifdef FULL
                  u64 *nodes,
#endif
                  SearchStack *restrict stack, const i32 pos_history_count,
                  const bool do_null) {
  assert(alpha < beta);
  assert(ply >= 0);

  const bool in_check =
      is_attacked(pos, lsb(pos->colour[0] & pos->pieces[King]), true);

  // IN-CHECK EXTENSION
  if (in_check) {
    depth++;
  }

  // EARLY EXITS
  if (depth > 4 && get_time() - start_time > max_time) {
    return alpha;
  }

  const u64 tt_hash = get_hash(pos);

  // FULL REPETITION DETECTION
  bool in_qsearch = depth <= 0;
  for (i32 i = pos_history_count + ply; !in_qsearch && i > 0 && ply > 0;
       i -= 2) {
    if (tt_hash == stack[i].position_hash) {
      return 0;
    }
  }

  // TT PROBING
  TTEntry *tt_entry = &tt[tt_hash % tt_length];
  const u16 tt_hash_partial = tt_hash / tt_length;
  Move tt_move = {0};
  if (tt_entry->partial_hash == tt_hash_partial) {
    tt_move = tt_entry->move;

    // TT PRUNING
    if (alpha == beta - 1 && tt_entry->depth >= depth &&
        tt_entry->flag != tt_entry->score <= alpha) {
      return tt_entry->score;
    }
  } else {
    // INTERNAL ITERATIVE REDUCTION
    depth -= depth > 3;
  }

  // STATIC EVAL WITH ADJUSTMENT FROM TT
  i32 static_eval = eval(pos);
  if (tt_entry->flag != static_eval > tt_entry->score &&
      tt_entry->partial_hash == tt_hash_partial) {
    static_eval = tt_entry->score;
  }

  // QUIESCENCE
  if (in_qsearch && static_eval > alpha) {
    if (static_eval >= beta) {
      return static_eval;
    }
    alpha = static_eval;
  }

  if (!in_qsearch && depth < 8 && alpha == beta - 1 && !in_check) {
    // REVERSE FUTILITY PRUNING
    if (static_eval - 47 * depth >= beta) {
      return static_eval;
    }

    // RAZORING
    in_qsearch = static_eval + 131 * depth <= alpha;
  }

  // NULL MOVE PRUNING
  if (depth > 2 && do_null && static_eval >= beta && alpha == beta - 1 &&
      !in_check) {
    Position npos = *pos;
    flip_pos(&npos);
    npos.ep = 0;
    if (-search(&npos, ply + 1, depth - 4, -beta, -alpha,
#ifdef FULL
                nodes,
#endif
                stack, pos_history_count, false) >= beta) {
      return beta;
    }
  }

  stack[ply].num_moves = movegen(pos, stack[ply].moves, in_qsearch);
  stack[ply].best_move = tt_move;
  stack[pos_history_count + ply + 2].position_hash = tt_hash;
  i32 moves_evaluated = 0;
  i32 quiets_evaluated = 0;
  u8 tt_flag = Upper;
  i32 best_score = in_qsearch ? static_eval : -inf;

  for (i32 move_index = 0; move_index < stack[ply].num_moves; move_index++) {
    i32 move_score = ~0x1010101LL; // Ends up as large negative

    // MOVE ORDERING
    for (i32 order_index = move_index; order_index < stack[ply].num_moves;
         order_index++) {
      assert(stack[ply].moves[order_index].takes_piece ==
             piece_on(pos, stack[ply].moves[order_index].to));
      const i32 order_move_score =
          ((i32)move_equal(&tt_move, &stack[ply].moves[order_index])
           << 30) // PREVIOUS BEST MOVE FIRST
          + (i32)stack[ply].moves[order_index].takes_piece * 921 +
          (i32)move_equal(&stack[ply].killer, &stack[ply].moves[order_index]) *
              915 // KILLER MOVE
          +
          move_history[pos->flipped][stack[ply].moves[order_index].takes_piece]
                      [stack[ply].moves[order_index].from]
                      [stack[ply].moves[order_index].to]; // HISTORY HEURISTIC
      if (order_move_score > move_score) {
        move_score = order_move_score;
        swapmoves(&stack[ply].moves[move_index],
                  &stack[ply].moves[order_index]);
      }
    }

    Position npos = *pos;
#ifdef FULL
    (*nodes)++;
#endif
    if (!makemove(&npos, &stack[ply].moves[move_index])) {
      continue;
    }

    // PRINCIPAL VARIATION SEARCH
    i32 low = moves_evaluated == 0 ? -beta : -alpha - 1;
    moves_evaluated++;

    // LATE MOVE REDCUCTION
    i32 reduction =
        depth > 1 && moves_evaluated > 6 ? 2 + moves_evaluated / 13 : 1;

    i32 score;
    while (true) {
      score = -search(&npos, ply + 1, depth - reduction, low, -alpha,
#ifdef FULL
                      nodes,
#endif
                      stack, pos_history_count, true);

      if (score <= alpha || (low == -beta && reduction == 1)) {
        break;
      }

      low = -beta;
      reduction = 1;
    }

    if (score > best_score) {
      best_score = score;
    }

    if (score > alpha) {
      stack[ply].best_move = stack[ply].moves[move_index];
      alpha = score;
      tt_flag = Exact;
      if (score >= beta) {
        tt_flag = Lower;
        assert(stack[ply].best_move.takes_piece ==
               piece_on(pos, stack[ply].best_move.to));
        i32 *const this_hist =
            &move_history[pos->flipped][stack[ply].best_move.takes_piece]
                         [stack[ply].best_move.from][stack[ply].best_move.to];
        *this_hist += depth * depth - depth * depth * *this_hist / 1024;
        for (i32 prev_index = 0; prev_index < move_index; prev_index++) {
          const Move prev = stack[ply].moves[prev_index];
          i32 *const prev_hist =
              &move_history[pos->flipped][prev.takes_piece][prev.from][prev.to];
          *prev_hist -= depth * depth + depth * depth * *prev_hist / 1024;
        }
        if (stack[ply].best_move.takes_piece == None) {
          stack[ply].killer = stack[ply].best_move;
        }
        break;
      }
    }

    if (stack[ply].moves[move_index].takes_piece == None) {
      quiets_evaluated++;
    }

    // LATE MOVE PRUNING
    if (!in_check && alpha == beta - 1 &&
        quiets_evaluated > 1 + depth * depth) {
      break;
    }
  }

  // MATE / STALEMATE DETECTION
  if (best_score == -inf) {
    return (ply - mate) * in_check;
  }

  *tt_entry = (TTEntry){.partial_hash = tt_hash_partial,
                        .move = stack[ply].best_move,
                        .score = best_score,
                        .depth = depth,
                        .flag = tt_flag};

  return best_score;
}

static void iteratively_deepen(
#ifdef FULL
    i32 maxdepth, u64 *nodes,
#endif
    Position *const restrict pos, SearchStack *restrict stack,
    const i32 pos_history_count) {
  start_time = get_time();
  __builtin_memset(move_history, 0, sizeof(move_history));
#ifdef FULL
  for (i32 depth = 1; depth < maxdepth; depth++) {
#else
  for (i32 depth = 1; depth < max_ply; depth++) {
#endif
    i32 score = search(pos, 0, depth, -inf, inf,
#ifdef FULL
                       nodes,
#endif
                       stack, pos_history_count, false);
    size_t elapsed = get_time() - start_time;

#ifdef FULL
    printf("info depth %i score cp %i time %i nodes %i", depth, score, elapsed,
           *nodes);
    if (elapsed > 0) {
      const u64 nps = *nodes * 1000 / elapsed;
      printf(" nps %i", nps);
    }

    putl(" pv ");
    char move_name[8];
    move_str(move_name, &stack[0].best_move, pos->flipped);
    putl(move_name);
    putl("\n");
#endif

    if (elapsed > max_time / 16) {
      break;
    }
  }
  char move_name[8];
  move_str(move_name, &stack[0].best_move, pos->flipped);
  putl("bestmove ");
  putl(move_name);
  putl("\n");
}

static void display_pos(Position *const pos) {
  Position npos = *pos;
  if (npos.flipped) {
    flip_pos(&npos);
  }
  for (i32 rank = 7; rank >= 0; rank--) {
    for (i32 file = 0; file < 8; file++) {
      i32 sq = rank * 8 + file;
      u64 bb = 1ULL << sq;
      i32 piece = piece_on(&npos, sq);
      if (bb & npos.colour[0]) {
        if (piece == Pawn) {
          putl("P");
        } else if (piece == Knight) {
          putl("N");
        } else if (piece == Bishop) {
          putl("B");
        } else if (piece == Rook) {
          putl("R");
        } else if (piece == Queen) {
          putl("Q");
        } else if (piece == King) {
          putl("K");
        }
      } else if (bb & npos.colour[1]) {
        if (piece == Pawn) {
          putl("p");
        } else if (piece == Knight) {
          putl("n");
        } else if (piece == Bishop) {
          putl("b");
        } else if (piece == Rook) {
          putl("r");
        } else if (piece == Queen) {
          putl("q");
        } else if (piece == King) {
          putl("k");
        }
      } else {
        putl(".");
      }
    }
    putl("\n");
  }
  putl("\nTurn: ");
  putl(pos->flipped ? "Black" : "White");
  putl("\nEval: ");
  i32 score = eval(pos);
  if (pos->flipped) {
    score = -score;
  }
  printf("%i\n", score);
}

#ifdef FULL
static void bench() {
  Position pos;
  i32 pos_history_count = 0;
#ifdef LOWSTACK
  SearchStack *stack = malloc(sizeof(SearchStack) * 1024);
#else
  SearchStack stack[1024];
#endif

  pos = (Position){.ep = 0,
                   .colour = {0xFFFFull, 0xFFFF000000000000ull},
                   .pieces = {0, 0xFF00000000FF00ull, 0x4200000000000042ull,
                              0x2400000000000024ull, 0x8100000000000081ull,
                              0x800000000000008ull, 0x1000000000000010ull},
                   .castling = {true, true, true, true}};
  max_time = 99999999999;
  u64 nodes = 0;
  const u64 start = get_time();
  iteratively_deepen(18, &nodes, &pos, stack, pos_history_count);
  const u64 end = get_time();
  const i32 elapsed = end - start;
  const u64 nps = elapsed ? 1000 * nodes / elapsed : 0;
  printf("%i nodes %i nps\n", nodes, nps);
}
#endif

#if !defined(FULL) && defined(NOSTDLIB)
void _start() {
#else
static void run() {
#endif
  char line[4096];
  Position pos;
  i32 pos_history_count;
#ifdef LOWSTACK
  SearchStack *stack = malloc(sizeof(SearchStack) * 1024);
#else
  SearchStack stack[1024];
#endif
  init_diag_masks();

#ifdef FULL
  pos = (Position){.ep = 0,
                   .colour = {0xFFFFull, 0xFFFF000000000000ull},
                   .pieces = {0, 0xFF00000000FF00ull, 0x4200000000000042ull,
                              0x2400000000000024ull, 0x8100000000000081ull,
                              0x800000000000008ull, 0x1000000000000010ull},
                   .castling = {true, true, true, true}};
  pos_history_count = 0;
#endif

#ifndef FULL
  // Assume first input is "uci"
  getl(line);
  putl("uciok\n");
#endif

  // UCI loop
  while (true) {
    getl(line);
#ifdef FULL
    u64 nodes = 0;
    if (!strcmp(line, "uci")) {
      putl("id name 4k.c\n");
      putl("id author Gediminas Masaitis\n");
      putl("\n");
      putl("option name Hash type spin default 1 min 1 max 1\n");
      putl("option name Threads type spin default 1 min 1 max 1\n");
      putl("uciok\n");
    } else if (!strcmp(line, "ucinewgame")) {
      __builtin_memset(tt, 0, tt_length * sizeof(TTEntry));
    } else if (!strcmp(line, "bench")) {
      bench();
    } else if (!strcmp(line, "gi")) {
      max_time = 99999999999;
      iteratively_deepen(max_ply, &nodes, &pos, stack, pos_history_count);
    } else if (!strcmp(line, "d")) {
      display_pos(&pos);
    } else if (!strcmp(line, "perft")) {
      char depth_str[4];
      getl(depth_str);
      const i32 depth = atoi(depth_str);
      const u64 start = get_time();
      nodes = perft(&pos, depth);
      const u64 end = get_time();
      const u64 elapsed = end - start;
      const u64 nps = elapsed ? 1000 * nodes / elapsed : 0;
      printf("info depth %i nodes %i time %i nps %i \n", depth, nodes, elapsed,
             nps);
    }
#endif
    if (line[0] == 'q') {
      exit_now();
    } else if (line[0] == 'i') {
      putl("readyok\n");
    } else if (line[0] == 'p') {
      pos = (Position){.ep = 0,
                       .colour = {0xFFFFull, 0xFFFF000000000000ull},
                       .pieces = {0, 0xFF00000000FF00ull, 0x4200000000000042ull,
                                  0x2400000000000024ull, 0x8100000000000081ull,
                                  0x800000000000008ull, 0x1000000000000010ull},
                       .castling = {true, true, true, true}};
      pos_history_count = 0;
      while (true) {
        const bool line_continue = getl(line);
        const i32 num_moves = movegen(&pos, stack[0].moves, false);
        for (i32 i = 0; i < num_moves; i++) {
          char move_name[8];
          move_str(move_name, &stack[0].moves[i], pos.flipped);
          assert(move_string_equal(line, move_name) ==
                 !strcmp(line, move_name));
          if (move_string_equal(line, move_name)) {
            stack[pos_history_count].position_hash = get_hash(&pos);
            pos_history_count++;
            if (stack[0].moves[i].takes_piece != None) {
              pos_history_count = 0;
            }
            makemove(&pos, &stack[0].moves[i]);
            break;
          }
        }
        if (!line_continue) {
          break;
        }
      }
    } else if (line[0] == 'g') {
#ifdef FULL
      while (true) {
        getl(line);
        if (!pos.flipped && !strcmp(line, "wtime")) {
          getl(line);
          max_time = atoi(line) / 2;
          break;
        } else if (pos.flipped && !strcmp(line, "btime")) {
          getl(line);
          max_time = atoi(line) / 2;
          break;
        } else if (!strcmp(line, "movetime")) {
          max_time = 20000; // Assume Lichess bot
          break;
        }
      }
      iteratively_deepen(max_ply, &nodes, &pos, stack, pos_history_count);
#else
      for (i32 i = 0; i < (pos.flipped ? 4 : 2); i++) {
        getl(line);
        max_time = atoi(line) / 2;
      }
      iteratively_deepen(&pos, stack, pos_history_count);
#endif
    }
  }
}

#if !defined(NOSTDLIB) || defined(FULL)
#ifdef NOSTDLIB
__attribute__((naked)) void _start() {
#ifdef FULL
  register long *stack asm("rsp");
  int argc = (int)*stack;
  char **argv = (char **)(stack + 1);
#endif
#else
int main(int argc, char **argv) {
#endif
#ifdef FULL
  if (argc > 1 && !strcmp(argv[1], "bench")) {
    init_diag_masks();
    bench();
    exit_now();
  }
#endif
  run();
}
#endif

#pragma endregion
