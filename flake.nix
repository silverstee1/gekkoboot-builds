{
  description = "A utilitarian bootloader for the GameCube";

  inputs = {
    flake-compat = {
      url = "github:edolstra/flake-compat";
      flake = false;
    };
    devkitNoob.url = "github:devkitNoob/devkitNoob";
    nixpkgs.follows = "devkitNoob/nixpkgs";
  };

  outputs = inputs: let
    inherit (inputs.nixpkgs) lib;
    inherit (lib) genAttrs;
    forAllSystems = fn:
      genAttrs systems (system:
        fn rec {
          inherit system;
          devkitNoob = inputs.devkitNoob.legacyPackages.${system};
          inherit (devkitNoob) nixpkgs;
        });
    systems = lib.systems.flakeExposed;
  in {
    formatter = forAllSystems ({nixpkgs, ...}: nixpkgs.alejandra);

    devShells = forAllSystems ({
      devkitNoob,
      nixpkgs,
      ...
    }: {
      default =
        devkitNoob.callPackage
        ({
          mkShell,
          linkFarm,
          noobkitPPC,
          gamecube-tools,
          meson,
          ninja,
          clang-tools,
          _7zz,
          python3,
        }:
          mkShell {
            name = "gekkoboot";
            nativeBuildInputs = [
              # The cross toolchain
              noobkitPPC
              gamecube-tools

              # The build system
              meson
              ninja

              # clang-format
              clang-tools

              # Compressing binaries and the release archive
              (linkFarm "_7-zip" {
                "bin/7z" = lib.getExe _7zz;
              })

              # For build scripts
              python3
            ];

            env = {
              # For the libogc2 Makefile
              DEVKITPPC = nixpkgs.callPackage ./nix/dkppc-rules.nix {};
            };
          })
        {};
    });
  };
}
