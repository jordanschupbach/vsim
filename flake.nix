{
  description = "VSim Linux development, Vulkan rendering, and Jolt physics";

  # Exact revision used by the initial NixOS development host. Generate flake.lock
  # with `nix flake lock` on a host with store/network access before updating it.
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/b6018f87da91d19d0ab4cf979885689b469cdd41";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      eachSystem = nixpkgs.lib.genAttrs systems;
      packageFor = system:
        let
          pkgs = import nixpkgs { inherit system; };
          dependencies = builtins.fromJSON (builtins.readFile ./dependencies.json);
          sourceFor = name: owner: repo: pkgs.fetchFromGitHub {
            inherit owner repo;
            rev = dependencies.${name}.revision;
            hash = dependencies.${name}.nix_hash;
          };
        in pkgs.stdenv.mkDerivation {
          pname = "vsim";
          version = "0.1.0";
          src = pkgs.lib.cleanSourceWith {
            src = ./.;
            filter = path: type:
              let name = baseNameOf path;
              in !(builtins.elem name [ "build" "dist" ".git" ".cache" ".direnv" ".agents" ".codex" ".aws" ])
                && pkgs.lib.cleanSourceFilter path type;
          };
          nativeBuildInputs = [ pkgs.cmake pkgs.ninja pkgs.pkg-config pkgs.python3 pkgs.just pkgs.shaderc ];
          buildInputs = [ pkgs.glm pkgs.nlohmann_json pkgs.lua5_4 pkgs.glfw pkgs.vulkan-headers pkgs.vulkan-loader ];
          cmakeFlags = [ "-DCMAKE_BUILD_TYPE=Release"
            "-DVSIM_NVRHI_SOURCE=${sourceFor "nvrhi" "NVIDIA-RTX" "NVRHI"}"
            "-DVSIM_JOLT_SOURCE=${sourceFor "jolt" "jrouwe" "JoltPhysics"}"
            "-DVSIM_VULKAN_HEADERS_SOURCE=${sourceFor "vulkan_headers" "KhronosGroup" "Vulkan-Headers"}"
          ];
          doCheck = true;
          checkPhase = ''
            runHook preCheck
            export VK_DRIVER_FILES="$(find ${pkgs.mesa}/share/vulkan/icd.d -name 'lvp_icd*.json' -print -quit)"
            export VK_LAYER_PATH="${pkgs.vulkan-validation-layers}/share/vulkan/explicit_layer.d"
            ctest --output-on-failure
            runHook postCheck
          '';
          # Both runtime executables and game scenes are installed, without editor code.
        };
    in {
      packages = eachSystem (system: { default = packageFor system; });
      checks = eachSystem (system: { core = packageFor system; });
      devShells = eachSystem (system:
        let pkgs = import nixpkgs { inherit system; };
        in {
          default = pkgs.mkShell {
            inputsFrom = [ (packageFor system) ];
            packages = [ pkgs.clang-tools pkgs.gdb pkgs.git pkgs.vulkan-tools
              pkgs.vulkan-validation-layers pkgs.glslang pkgs.shaderc pkgs.python3Packages.pyyaml ];
            shellHook = ''
              export VSIM_SOFTWARE_VULKAN_ICD="$(find ${pkgs.mesa}/share/vulkan/icd.d -name 'lvp_icd*.json' -print -quit)"
              export VK_LAYER_PATH="${pkgs.vulkan-validation-layers}/share/vulkan/explicit_layer.d''${VK_LAYER_PATH:+:$VK_LAYER_PATH}"
              echo "VSim: just game | just editor | just test | just --list"
            '';
          };
        });
    };
}
