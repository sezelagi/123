contract A {
    function b() public returns (uint i) {
        assembly {
            i := 3_200_000_000
        }
    }

    function c() public returns (uint i) {
        assembly {
            i := 3200000000
        }
    }

    function d() public returns (int i) {
        assembly {
            i := 3_200_000_000
        }
    }

    function e() public returns (int i) {
        assembly {
            i := 32e8
        }
    }

    function f() public returns (string memory j) {
        string memory s = "string_with_underscores";
        assembly {
            j := s
        }
    }
}
// ====
// compileToEwasm: also
// ----
// b() -> 3200000000
// c() -> 3200000000
// d() -> 3200000000
// e() -> 3200000000
// f() -> 0x20, 23, "string_with_underscores"