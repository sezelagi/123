{
  sstore(0, keccak256(0, 0))
  sstore(0, keccak256(0, 0x20))
}
// ----
// Trace:
//   STATICCALL()
//   STATICCALL()
// Memory dump:
//     20: 0000000000000000000000000000000000000000000000000000000000000001
// Storage dump:
//   0000000000000000000000000000000000000000000000000000000000000000: 0000000000000000000000000000000000000000000000000000000000000001
