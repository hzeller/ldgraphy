# This is a nix-shell for use with the nix package manager.
# If you have nix installed, you may simply run `nix-shell`
# in this repo, and have all dependencies ready in the new shell.

{ pkgs ? import <nixpkgs> {} }:
pkgs.mkShell {
  buildInputs = with pkgs;
    [
      # Dependencies for LDGraphy application
      libpng

      # CAD for all laser-cut parts
      ghostscript # 2D CAD script :)
      pstoedit    # Generate DXF from it
      librecad    # mostly to just look at generated dxf

      # PCBs
      kicad
      python3     # For PCB cam scripts

      # CAM
      lightburn
    ];
  shellHook = ''
    # If not compiled on a beaglebone while developing,
    # don't set the compile flags needed for arm.
    export ARM_COMPILE_FLAGS=""
  '';
}
