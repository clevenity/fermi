{
  description = "Fermi — a new experimental programming lang";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in {
        packages.default = pkgs.stdenv.mkDerivation {
          pname   = "fermi";
          version = "0.1.0";
          src     = ./.;

          nativeBuildInputs = [ pkgs.gcc pkgs.binutils pkgs.gnumake ];
          buildInputs       = [];

          buildPhase = ''
            make build PREFIX=$out
          '';
          installPhase = ''
            make install PREFIX=$out DESTDIR=""
          '';

          meta = {
            description   = "Fermi AOT compiler";
            license       = pkgs.lib.licenses.asl20;
            maintainers   = [];
            platforms     = [ "x86_64-linux" "aarch64-linux" ];
          };
        };

        devShells.default = pkgs.mkShell {
          packages = with pkgs; [
            gcc binutils gnumake clang llvmPackages.llvm
            hyperfine valgrind gdb
          ];
          shellHook = ''
            echo "fermi dev shell — run 'make build' to compile"
          '';
        };
      });
}
