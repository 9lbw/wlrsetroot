{
  description = "wlrsetroot - XBM wallpaper setter for wlroots compositors";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in
      {
        devShells.default = pkgs.mkShell {
          buildInputs = with pkgs; [
            # Build tools
            meson
            ninja
            pkg-config

            # Wayland dependencies
            wayland
            wayland-protocols
            wayland-scanner

            gdb
            valgrind
          ];

        };

        packages.default = pkgs.stdenv.mkDerivation {
          pname = "wlrsetroot";
          version = "0.1.0";

          src = ./.;

          nativeBuildInputs = with pkgs; [
            meson
            ninja
            pkg-config
            wayland-scanner
          ];

          buildInputs = with pkgs; [
            wayland
            wayland-protocols
          ];

          meta = with pkgs.lib; {
            description = "XBM wallpaper setter for wlroots compositors";
            license = licenses.gpl3;
            platforms = platforms.linux;
          };
        };
      }
    );
}
